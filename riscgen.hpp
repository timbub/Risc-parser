#pragma once

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <algorithm>
#include <bitset>
#include "json.hpp"

namespace risc_gen {
    class RiscGen{
    private:
        struct original_field {
            std::string name;
            int min_width =0;
            bool is_flexible = false;
        };

        struct original_instructions {
            std::vector<std::string> insns;
            std::vector<std::string> operands;
            std::string format;
            std::string comment;
        };

        struct encoded_field {
            int msb{0};
            int lsb{0};
            std::string value;
        };

        struct encoded_instruction {
            std::string insn;
            std::map<std::string, encoded_field> fields;
        };

    private:
        int length_ = 0;
        std::unordered_map<std::string, original_field> field_rules_;
        std::vector<original_instructions> groups_;
        int bits_f_ = 0;
        int bits_opcode_ = 0;

        std::map<std::string, std::pair<int, int>> global_layout_;
    private:
        void fill_RES(encoded_instruction& res) {
            std::vector<int> bit_used(length_, 0);

            for (const auto& pair : res.fields) {
                const encoded_field& field = pair.second;
                if (field.lsb < 0 || field.msb >= length_) {
                throw std::runtime_error("Error of fill: Instruction '" + res.insn +
                 "' does not fit in " + std::to_string(length_) + " bit");
                }

                for (int b = field.lsb; b <= field.msb; ++b) {
                    bit_used[b] = 1;
                }
            }

            int res_counter = 0;
            int hole_start = -1;

            for (int b = length_ - 1; b >= 0; --b) {
                if (bit_used[b] == 0) {
                    if (hole_start == -1) {
                        hole_start = b;
                    }
                } else {
                    if (hole_start != -1) {
                        std::string res_name = "RES" + std::to_string(res_counter);
                        res_counter++;

                        res.fields[res_name] = { hole_start, b + 1, to_bin(0, hole_start - b) };
                        hole_start = -1;
                    }
                }
            }
            if (hole_start != -1) {
                std::string res_name = "RES" + std::to_string(res_counter);
                res.fields[res_name] = { hole_start, 0, to_bin(0, hole_start + 1) };
            }
        }
    private:
        void calculate_sizes() {
            bits_f_ = static_cast<int>(std::ceil(std::log2(groups_.size())));

            size_t max_insns = 0;
            for (const auto& g : groups_) {
                max_insns = std::max(max_insns, g.insns.size());
            }
            bits_opcode_ = static_cast<int>(std::ceil(std::log2(max_insns)));
        }

        int get_min_width(const std::string& name) const {
            auto it = field_rules_.find(name);
            if (it != field_rules_.end()) {
                return it->second.min_width;
            }
            return 0;
        }

        void build_layout() {
            int normal_start_bit = length_ - bits_f_ - bits_opcode_ - 1;

            for (const auto& group : groups_) {
                if (std::find(group.operands.begin(), group.operands.end(), "code") != group.operands.end()) {
                    global_layout_["code"] = { length_ - bits_f_ - 1, length_ - bits_f_ - bits_opcode_ };
                }
            }

            for (const auto& group : groups_) {
                int cursor = normal_start_bit;

                for (const auto& op_name : group.operands) {
                    if (op_name == "code") continue;

                    if (global_layout_.find(op_name) == global_layout_.end()) {
                        int w = get_min_width(op_name);
                        global_layout_[op_name] = {cursor, cursor - w + 1};
                    }
                    cursor = global_layout_[op_name].second - 1;
                }
            }
        }

        std::string to_bin(int val, int width) const {
            if (width <= 0) return "";
            std::string s = std::bitset<32>(val).to_string();
            return s.substr(32 - width);
        }

        void load_config(const std::string& filename) {
            std::ifstream file(filename);
            if (!file.is_open()) throw std::runtime_error("File not found: " + filename);
            nlohmann::json json;
            file >> json;
            length_ = std::stoi(json.at("length").get<std::string>());
            for (const auto& item : json.at("fields")) {
                for (auto& [name, val_raw] : item.items()) {
                    original_field field;
                    field.name = name;
                    std::string val = val_raw.get<std::string>();
                    if (val.starts_with(">=")) {
                        field.is_flexible = true;
                        field.min_width = std::stoi(val.substr(2));
                    } else {
                        field.is_flexible = false;
                        field.min_width = std::stoi(val);
                    }
                    field_rules_[name] = std::move(field);
                }
            }

            for (const auto& item : json.at("instructions")) {
                original_instructions inst;
                inst.format = item.at("format");
                inst.insns = item.at("insns").get<std::vector<std::string>>();
                inst.operands = item.at("operands").get<std::vector<std::string>>();
                groups_.push_back(std::move(inst));
            }
        }

        std::vector<encoded_instruction> create_instructions() {
            calculate_sizes();
            build_layout();

            std::vector<encoded_instruction> results;

            for (size_t g_idx = 0; g_idx < groups_.size(); ++g_idx) {
                const auto& group = groups_[g_idx];

                for (size_t i_idx = 0; i_idx < group.insns.size(); ++i_idx) {
                    encoded_instruction res;
                    res.insn = group.insns[i_idx];

                    res.fields["F"] = { length_ - 1, length_ - bits_f_, to_bin(g_idx, bits_f_) };

                    bool has_code = std::find(group.operands.begin(), group.operands.end(), "code") != group.operands.end();
                    if (!has_code) {
                        res.fields["OPCODE"] = { length_ - bits_f_ - 1, length_ - bits_f_ - bits_opcode_, to_bin(i_idx, bits_opcode_) };
                    }

                    int current_lsb = length_ - bits_f_ - (has_code ? 0 : bits_opcode_);

                    for (const auto& op : group.operands) {
                        auto [msb, lsb] = global_layout_[op];
                        res.fields[op] = { msb, lsb, "+" };
                        current_lsb = std::min(current_lsb, lsb);
                    }

                    if (current_lsb > 0) {
                        std::string field_to_expand = "";

                        for (const auto& op : group.operands) {
                            if (field_rules_[op].is_flexible && res.fields[op].lsb == current_lsb) {
                                field_to_expand = op;
                                break;
                            }
                        }

                        if (!field_to_expand.empty()) {
                            res.fields[field_to_expand].lsb = 0;
                        }
                    }

                    fill_RES(res);
                    results.push_back(std::move(res));
                }
            }
            return results;
        }

        static bool compare_fields_by_msb(const std::pair<std::string, risc_gen::RiscGen::encoded_field>& a,
                                   const std::pair<std::string, risc_gen::RiscGen::encoded_field>& b) {
            return a.second.msb > b.second.msb;
        }

        void save_to_json(const std::vector<risc_gen::RiscGen::encoded_instruction>& instructions,
                                       const std::string& filename) {
            nlohmann::json root_array = nlohmann::json::array();

            for (const auto& inst: instructions) {

                nlohmann::json json_instruction;
                json_instruction["insn"] = inst.insn;

                std::vector<std::pair<std::string, risc_gen::RiscGen::encoded_field>> sorted_fields;

                for (auto it = inst.fields.begin(); it != inst.fields.end(); ++it) {
                    sorted_fields.push_back(std::make_pair(it->first, it->second));
                }

                std::sort(sorted_fields.begin(), sorted_fields.end(), compare_fields_by_msb);

                nlohmann::json json_fields = nlohmann::json::array();
                for (const auto& [name, data] : sorted_fields) {
                    nlohmann::json field_content;
                    field_content["msb"] = data.msb;
                    field_content["lsb"] = data.lsb;
                    field_content["value"] = data.value;

                    nlohmann::json field;
                    field[name] = field_content;

                    json_fields.push_back(field);
                }

                json_instruction["fields"] = json_fields;
                root_array.push_back(json_instruction);
            }

            std::ofstream output_file(filename);
            if (!output_file.is_open()) {
                throw std::runtime_error("Failed to open output file: " + filename);
            }

            output_file << root_array.dump(4) << std::endl;
        }
    public:
        void generate_instructions_system(const std::string& input, const std::string& output) {
            load_config(input);
            auto instructions = create_instructions();
            save_to_json(instructions, output);
        }
    };

};

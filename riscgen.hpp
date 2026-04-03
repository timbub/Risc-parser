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
        std::vector<original_field> field_rules_;
        std::vector<original_instructions> groups_;
        int bits_f_ = 0;
        int bits_opcode_ = 0;

        std::map<std::string, std::pair<int, int>> global_layout_;

    public:
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
                        field.min_width = std::stoi(val);
                    }
                    field_rules_.push_back(std::move(field));
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

        std::vector<encoded_instruction> pack() {
            calculate_metrics();
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
                        res.fields["RES0"] = { current_lsb - 1, 0, to_bin(0, current_lsb) };
                    }
                    results.push_back(std::move(res));
                }
            }
            return results;
        }
    };

};

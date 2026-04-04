#include "riscgen.hpp"

int main(int argc, char* argv[]) {
try {
    if (argc != 3) {
        std::cerr << "Write: <JSON_input> <JSON_output>\n";
    }

    std::string json_input = argv[1];
    std::string json_output = argv[2];
    risc_gen::RiscGen generate;
    generate.generate_instructions_system(json_input, json_output);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

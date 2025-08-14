#include <iostream>
#include "LLM.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <model_path> [mmproj_path] [image_path]\n", argv[0]);
        return 1;
    }

    LLM llm;

    if (!llm.load(argv[1], (argc > 2) ? argv[2] : "", 0)) {
        return 1;
    }
    std::cout << "Model loaded successfully." << std::endl;
    std::string user_input = "什么是大模型";
    std::string image_path = (argc > 3) ? argv[3] : "";
    std::string response = llm.send(user_input, image_path);

    std::cout << "Response: " << response << std::endl;

    llm.unload();

    return 0;
}

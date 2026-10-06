// ttmod CLI (portable dev/advanced tool; normal users just drop mods/).
//   ttmod detect <exe>
//   ttmod package create <moddir> <out.ttmod>
//   ttmod package validate <file.ttmod>
//   ttmod package info <file.ttmod>
//   ttmod mods list <gamedir>
//   ttmod mods info <gamedir> <id>
//   ttmod mods enable|disable <gamedir> <id>
#include <cstdio>
#include <cstring>
#include <string>

#include "commands.hpp"

int cmd_usage() {
    std::puts("usage: ttmod <detect|package|inspect|map> ...\n"
              "  detect <exe>\n"
              "  package create <moddir> <out.ttmod>\n"
              "  package validate <file>\n"
              "  package info <file>\n"
              "  inspect <file.ttarch2>\n"
              "  map <ttmod.log>");
    return 2;
}

int main(int argc, char** argv) {
    if (argc < 2) return cmd_usage();
    std::string cmd = argv[1];
    if (cmd == "detect") return cmd_detect(argc - 2, argv + 2);
    if (cmd == "package") return cmd_package(argc - 2, argv + 2);
    if (cmd == "inspect") return cmd_inspect(argc - 2, argv + 2);
    if (cmd == "map") return cmd_map(argc - 2, argv + 2);
    if (cmd == "mods") return cmd_mods(argc - 2, argv + 2);
    return cmd_usage();
}

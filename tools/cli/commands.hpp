#pragma once
// CLI command entry points (Stage 19). Each takes its own argv tail
// (argv[0] = subcommand or first operand). Returns process exit code.
int cmd_usage();
int cmd_detect(int argc, char** argv);
int cmd_package(int argc, char** argv);
int cmd_inspect(int argc, char** argv);
int cmd_map(int argc, char** argv);
int cmd_mods(int argc, char** argv);

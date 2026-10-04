#pragma once

/** If argv asks for --help / --version, print to stdout and return true (caller exits 0). */
bool hovHandleHelpOrVersion(int argc, char **argv);

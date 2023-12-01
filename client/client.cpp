
// ----------------------------------------------------------------------------
//                              Apache License
//                        Version 2.0, January 2004
//                     http://www.apache.org/licenses/
//
// This file is part of Shared-State(https://github.com/niXman/shared-state) project.
//
// This was a test task for implementing multithreaded Shared-State server using asio.
//
// Copyright (c) 2023 niXman (github dot nixman dog pm.me). All rights reserved.
// ----------------------------------------------------------------------------

#include "cmdargs/cmdargs.hpp"

#include <iostream>

/**********************************************************************************************************************/

struct: cmdargs::kwords_group {
    CMDARGS_OPTION_ADD(ip, std::string, "server IP", and_(port));
    CMDARGS_OPTION_ADD(port, std::uint16_t, "server PORT", and_(ip));
    CMDARGS_OPTION_ADD(save, bool, "save the state to file?", optional, and_(fname));
    CMDARGS_OPTION_ADD(fname, std::string, "the state file name", optional, and_(save));
    CMDARGS_OPTION_ADD(ping, std::size_t, "ping interval in seconds", optional);

    CMDARGS_OPTION_ADD_HELP();
} const kwords;

int main(int argc, char **argv) try {
    std::string error;
    const auto args = cmdargs::parse_args(&error, argc, argv, kwords);
    if ( !error.empty() ) {
        std::cerr << "command line error: " << error << std::endl;

        return EXIT_FAILURE;
    }
    if ( args.is_set(kwords.help) ) {
        cmdargs::show_help(std::cout, argv[0], args);

        return EXIT_SUCCESS;
    }

    args.dump(std::cout);

    return EXIT_SUCCESS;
} catch (const std::exception &ex) {
    std::cerr << "std::exception: " << ex.what() << std::endl;

    return EXIT_FAILURE;
}

/**********************************************************************************************************************/

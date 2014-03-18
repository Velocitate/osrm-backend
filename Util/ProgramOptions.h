/*

Copyright (c) 2013, Project OSRM, Dennis Luxen, others
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

Redistributions of source code must retain the above copyright notice, this list
of conditions and the following disclaimer.
Redistributions in binary form must reproduce the above copyright notice, this
list of conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#ifndef PROGAM_OPTIONS_H
#define PROGAM_OPTIONS_H

#include "GitDescription.h"
#include "OSRMException.h"
#include "SimpleLogger.h"

#include <osrm/ServerPaths.h>

#include <boost/any.hpp>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <boost/regex.hpp>
#include <boost/unordered_map.hpp>

#include <fstream>
#include <string>

// support old capitalized option names by down-casing them with a regex replace
inline void PrepareConfigFile(
    const boost::filesystem::path& path,
    std::string& output
) {
    std::ifstream config_stream( path.string().c_str() );
    std::string input_str(
        (std::istreambuf_iterator<char>(config_stream)),
        std::istreambuf_iterator<char>()
    );
    boost::regex regex( "^([^=]*)" );    //match from start of line to '='
    std::string format( "\\L$1\\E" );    //replace with downcased substring
    output = boost::regex_replace( input_str, regex, format );
}

// generate boost::program_options object for the routing part
inline bool GenerateServerProgramOptions(
    const int argc,
    const char * argv[],
    ServerPaths & paths,
    std::string & ip_address,
    int & ip_port,
    int & requested_num_threads,
    bool & use_shared_memory
) {

    // declare a group of options that will be allowed only on command line
    boost::program_options::options_description generic_options("Options");
    generic_options.add_options()
        ("version,v", "Show version")
        ("help,h", "Show this help message")
        (
            "config,c",
            boost::program_options::value<boost::filesystem::path>(
                &paths["config"]
            )->default_value("server.ini"),
            "Path to a configuration file"
        );

    // declare a group of options that will be allowed both on command line
    // as well as in a config file
    boost::program_options::options_description config_options("Configuration");
    config_options.add_options()
        (
            "hsgrdata",
            boost::program_options::value<boost::filesystem::path>(&paths["hsgrdata"]),
            ".hsgr file"
        )
        (
            "nodesdata",
            boost::program_options::value<boost::filesystem::path>(&paths["nodesdata"]),
            ".nodes file"
        )
        (
            "edgesdata",
            boost::program_options::value<boost::filesystem::path>(&paths["edgesdata"]),
            ".edges file")
        (
            "ramindex",
            boost::program_options::value<boost::filesystem::path>(&paths["ramindex"]),
            ".ramIndex file")
        (
            "fileindex",
            boost::program_options::value<boost::filesystem::path>(&paths["fileindex"]),
            "File index file")
        (
            "namesdata",
            boost::program_options::value<boost::filesystem::path>(&paths["namesdata"]),
            ".names file")
        (
            "timestamp",
            boost::program_options::value<boost::filesystem::path>(&paths["timestamp"]),
            ".timestamp file")
        (
            "ip,i",
            boost::program_options::value<std::string>(&ip_address)->default_value("0.0.0.0"),
            "IP address"
        )
        (
            "port,p",
            boost::program_options::value<int>(&ip_port)->default_value(5000),
            "TCP/IP port"
        )
        (
            "threads,t",
            boost::program_options::value<int>(&requested_num_threads)->default_value(8),
            "Number of threads to use"
        )
        (
            "sharedmemory,s",
            boost::program_options::value<bool>(&use_shared_memory)->default_value(false),
            "Load data from shared memory"
        );

    // hidden options, will be allowed both on command line and in config
    // file, but will not be shown to the user
    boost::program_options::options_description hidden_options("Hidden options");
    hidden_options.add_options()
        (
            "base,b",
            boost::program_options::value<boost::filesystem::path>(&paths["base"]),
            "base path to .osrm file"
        );

    // positional option
    boost::program_options::positional_options_description positional_options;
    positional_options.add("base", 1);

    // combine above options for parsing
    boost::program_options::options_description cmdline_options;
    cmdline_options.add(generic_options).add(config_options).add(hidden_options);

    boost::program_options::options_description config_file_options;
    config_file_options.add(config_options).add(hidden_options);

    boost::program_options::options_description visible_options(
        boost::filesystem::basename(argv[0]) + " <base.osrm> [<options>]"
    );
    visible_options.add(generic_options).add(config_options);

    // parse command line options
    boost::program_options::variables_map option_variables;
    boost::program_options::store(
        boost::program_options::command_line_parser(argc, argv).options(cmdline_options).positional(positional_options).run(),
        option_variables
    );

    if(option_variables.count("version")) {
        SimpleLogger().Write() << g_GIT_DESCRIPTION;
        return false;
    }

    if(option_variables.count("help")) {
        SimpleLogger().Write() << visible_options;
        return false;
    }

    boost::program_options::notify(option_variables);

    // parse config file
    ServerPaths::iterator path_iterator = paths.find("config");
    if(
        path_iterator != paths.end() &&
        boost::filesystem::is_regular_file(path_iterator->second) &&
        !option_variables.count("base")
    ) {
        SimpleLogger().Write() <<
            "Reading options from: " << path_iterator->second.string();
        std::string config_str;
        PrepareConfigFile( path_iterator->second, config_str );
        std::stringstream config_stream( config_str );
        boost::program_options::store(
            parse_config_file(config_stream, config_file_options),
            option_variables
        );
        boost::program_options::notify(option_variables);
    }

    if( 1 > requested_num_threads ) {
        throw OSRMException("Number of threads must be a positive number");
    }

    if( !use_shared_memory && option_variables.count("base") ) {
        path_iterator = paths.find("base");
        BOOST_ASSERT( paths.end() != path_iterator );
        std::string base_string = path_iterator->second.string();

        path_iterator = paths.find("hsgrdata");
        if(
            path_iterator != paths.end() &&
            !boost::filesystem::is_regular_file(path_iterator->second)
        ) {
            path_iterator->second = base_string + ".hsgr";
        } else {
            throw OSRMException(base_string + ".hsgr not found");
        }

        path_iterator = paths.find("nodesdata");
        if(
            path_iterator != paths.end() &&
            !boost::filesystem::is_regular_file(path_iterator->second)
        ) {
            path_iterator->second = base_string + ".nodes";
        } else {
            throw OSRMException(base_string + ".nodes not found");
        }


        path_iterator = paths.find("edgesdata");
        if(
            path_iterator != paths.end() &&
            !boost::filesystem::is_regular_file(path_iterator->second)
        ) {
            path_iterator->second = base_string + ".edges";
        } else {
            throw OSRMException(base_string + ".edges not found");
        }


        path_iterator = paths.find("ramindex");
        if(
            path_iterator != paths.end() &&
            !boost::filesystem::is_regular_file(path_iterator->second)
        ) {
            path_iterator->second = base_string + ".ramIndex";
        } else {
            throw OSRMException(base_string + ".ramIndex not found");
        }


        path_iterator = paths.find("fileindex");
        if(
            path_iterator != paths.end() &&
            !boost::filesystem::is_regular_file(path_iterator->second)
        ) {
            path_iterator->second = base_string + ".fileIndex";
        } else {
            throw OSRMException(base_string + ".fileIndex not found");
        }


        path_iterator = paths.find("namesdata");
        if(
            path_iterator != paths.end() &&
            !boost::filesystem::is_regular_file(path_iterator->second)
        ) {
            path_iterator->second = base_string + ".names";
        } else {
            throw OSRMException(base_string + ".namesIndex not found");
        }

        path_iterator = paths.find("timestamp");
        if(
            path_iterator != paths.end() &&
            !boost::filesystem::is_regular_file(path_iterator->second)
        ) {
            path_iterator->second = base_string + ".timestamp";
        }

        return true;
    }
    if (use_shared_memory && !option_variables.count("base"))
    {
        return true;
    }
    SimpleLogger().Write() << visible_options;
    return false;
}

#endif /* PROGRAM_OPTIONS_H */

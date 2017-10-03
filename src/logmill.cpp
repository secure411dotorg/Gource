/*
    Copyright (C) 2012 Andrew Caudwell (acaudwell@gmail.com)

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version
    3 of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "logmill.h"
#include "gource_settings.h"

#include "formats/custom.h"
#include "formats/mal4s.h"

#include <boost/filesystem.hpp>

extern "C" {

    static int logmill_thread(void *lmill) {

        RLogMill *logmill = static_cast<RLogMill*> (lmill);
        logmill->run();

        return 0;
    }

};

RLogMill::RLogMill(const std::string& logfile)
    : logfile(logfile) {

    logmill_thread_state = LOGMILL_STATE_STARTUP;
    clog = 0;

#if SDL_VERSION_ATLEAST(2,0,0)
    thread = SDL_CreateThread( logmill_thread, "logmill", this );
#else
    thread = SDL_CreateThread( logmill_thread, this );
#endif
}

RLogMill::~RLogMill() {

    abort();

    if(clog) delete clog;
}

void RLogMill::run() {
    logmill_thread_state = LOGMILL_STATE_FETCHING;

#if defined(HAVE_PTHREAD) && !defined(_WIN32)
    sigset_t mask;
    sigemptyset(&mask);

    // unblock SIGINT so user can cancel
    // NOTE: assumes SDL is using pthreads

    sigaddset(&mask, SIGINT);
    pthread_sigmask(SIG_UNBLOCK, &mask, 0);
#endif

    std::string log_format = gGourceSettings.log_format;

    try {

        clog = fetchLog(log_format);

        // find first commit after start_timestamp if specified
        if(clog != 0 && gGourceSettings.start_timestamp != 0) {

            RCommit commit;

            while(!gGourceSettings.shutdown && !clog->isFinished()) {

                if(clog->nextCommit(commit) && commit.timestamp >= gGourceSettings.start_timestamp) {
                    clog->bufferCommit(commit);
                    break;
                }
            }
        }

    } catch(SeekLogException& exception) {
        error = "unable to read log file";
    } catch(SDLAppException& exception) {
        error = exception.what();
    }

    if(!clog && error.empty()) {
        if(boost::filesystem::is_directory(logfile)) {
            if(!log_format.empty()) {
                
                if(gGourceSettings.start_timestamp || gGourceSettings.stop_timestamp) {
                    error = "failed to generate log file for the specified time period";
                } else {
                    error = "failed to generate log file";
                }
#ifdef _WIN32
            // no error - should trigger help message
            } else if(gGourceSettings.default_path && boost::filesystem::exists("./gource.exe")) {
                error = "";
#endif
            } else {
                error = "directory not supported";
            }
        } else {
            error = "unsupported log format (you may need to regenerate your log file)";
        }
    }

    logmill_thread_state = clog ? LOGMILL_STATE_SUCCESS : LOGMILL_STATE_FAILURE;
}

void RLogMill::abort() {
    if(!thread) return;

    // TODO: make abort nicer by notifying the log process
    //       we want to shutdown
    SDL_WaitThread(thread, 0);
    
    thread = 0;
}

bool RLogMill::isFinished() {
    return logmill_thread_state > LOGMILL_STATE_FETCHING;
}

int RLogMill::getStatus() {
    return logmill_thread_state;
}

std::string RLogMill::getError() {
    return error;
}


RCommitLog* RLogMill::getLog() {

    if(thread != 0) {
        SDL_WaitThread(thread, 0);
        thread = 0;        
    }
    
    return clog;
}

RCommitLog* RLogMill::fetchLog(std::string& log_format) {

    RCommitLog* clog = 0;

    //if the log format is not specified and 'logfile' is a directory, recursively look for a version control repository.
    //this method allows for something strange like someone who having an svn repository inside a git repository
    //(in which case it would pick the svn directory as it would encounter that first)

    if(log_format.empty() && logfile != "-") fprintf(stderr, "FATAL: No log specified and no log read from STDIN."); //{

/*        try {
            boost::filesystem::path repo_path(logfile);

            if(is_directory(repo_path)) {
                if(findRepository(repo_path, log_format)) {
                    logfile = repo_path.string();
                }
            }


        } catch(boost::filesystem::filesystem_error& error) {
        }
    }
*/
    //we've been told what format to use
    if(log_format.size() > 0) {
        debugLog("log-format = %s", log_format.c_str());

        if(log_format == "mal4s") {
            clog = new Mal4sLog(logfile);
            if(clog->checkFormat()) return clog;
            delete clog;
        }

        if(log_format == "custom") {
            clog = new CustomLog(logfile);
            if(clog->checkFormat()) return clog;
            delete clog;
        }
        return 0;
    }

    // try different formats until one works
    //mal4s
    debugLog("trying mal4s...");
    clog = new Mal4sLog(logfile);
    if(clog->checkFormat()) return clog;

    delete clog;

    //custom
    debugLog("trying custom...");
    clog = new CustomLog(logfile);
    if(clog->checkFormat()) return clog;

    delete clog;

    return 0;
}

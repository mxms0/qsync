#include "qsync.h"

#include <deque>

using namespace std;
namespace fs = std::filesystem;

const auto FileChunkSize = 100;

bool
FindFiles(
    const string& Root,
    FileResultsCallback& Callback)
{
    error_code Error;
    fs::path RootPath{Root};
    if (!fs::exists(RootPath, Error) || !fs::is_directory(RootPath, Error)) {
        return false;
    }
    if (Error) {
        return false;
    }

    bool Result = true;
    deque<fs::path> UnexploredDirs{};
    UnexploredDirs.push_back(RootPath);

    while (!UnexploredDirs.empty()) {
        vector<u8string> Directories;
        vector<u8string> Files;
        auto CurrentDirectory = UnexploredDirs.front();
        UnexploredDirs.pop_front();
        for (auto DirItem : fs::directory_iterator{CurrentDirectory, fs::directory_options::skip_permission_denied | fs::directory_options::follow_directory_symlink, Error}) {
            if (Error) {
                // todo: print error
                Result = false;
                break;
            }
            if (DirItem.is_directory(Error)) {
                if (Error) {
                    // todo: print error
                    Result = false;
                    continue;
                }
                UnexploredDirs.push_back(DirItem.path());
                Directories.push_back(DirItem.path().u8string());
            } else if (DirItem.is_regular_file(Error)) {
                if (Error) {
                    // todo: print error
                    Result = false;
                    continue;
                }
                Files.push_back(DirItem.path().u8string());
            } else if (DirItem.is_symlink(Error)) {
                if (Error) {
                    // todo: print error
                    Result = false;
                    continue;
                }
                auto LinkPath = fs::read_symlink(DirItem.path(), Error);
                if (Error) {
                    // todo: print error
                    Result = false;
                    continue;
                }
                if (fs::is_directory(LinkPath, Error)) {
                    if (Error) {
                        // todo: print error
                        Result = false;
                        continue;
                    }
                    UnexploredDirs.push_back(LinkPath);
                    Directories.push_back(LinkPath.u8string());
                } else if (fs::is_regular_file(LinkPath, Error)) {
                    if (Error) {
                        // todo: print error
                        Result = false;
                        continue;
                    }
                    Files.push_back(LinkPath.u8string());
                }
            }
            if (Files.size() >= FileChunkSize || Directories.size() >= FileChunkSize) {
                Callback(Files, Directories);
                Files.clear();
                Directories.clear();
            }
        }
        if (Files.size() || Directories.size()) {
            Callback(Files, Directories);
        }
    }

    return Result;
}
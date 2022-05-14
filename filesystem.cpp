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
    deque<fs::path> Items{};
    Items.push_back(RootPath);

    while (!Items.empty()) {
        vector<u8string> Directories;
        vector<u8string> Files;
        auto CurrentDirectory = Items.front();
        Items.pop_front();
        for (auto SubItem : fs::directory_iterator{CurrentDirectory, fs::directory_options::skip_permission_denied | fs::directory_options::follow_directory_symlink, Error}) {
            if (Error) {
                // todo: print error
                Result = false;
                break;
            }
            if (SubItem.is_directory(Error)) {
                if (Error) {
                    // todo: print error
                    Result = false;
                    continue;
                }
                Items.push_back(SubItem.path());
                Directories.push_back(SubItem.path().u8string());
            } else if (SubItem.is_regular_file(Error)) {
                if (Error) {
                    // todo: print error
                    Result = false;
                    continue;
                }
                Files.push_back(SubItem.path().u8string());
            } else if (SubItem.is_symlink(Error)) {
                if (Error) {
                    // todo: print error
                    Result = false;
                    continue;
                }
                auto LinkPath = fs::read_symlink(SubItem.path(), Error);
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
                    Items.push_back(LinkPath);
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
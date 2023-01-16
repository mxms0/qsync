#include "qsync.h"

#include <deque>
#include <future>

using namespace std;
namespace fs = std::filesystem;

const auto FileChunkSize = 100;

tuple<vector<fs::path>, vector<fs::path>, bool>
ProcessFolder(
    const fs::path& Parent)
{
    bool Success = true;
    error_code Error;
    vector<fs::path> Directories{};
    vector<fs::path> Files{};
    for (auto DirItem : fs::directory_iterator{Parent, fs::directory_options::skip_permission_denied | fs::directory_options::follow_directory_symlink, Error}) {
        if (Error) {
            // todo: print error
            Success = false;
            break;
        }
        if (DirItem.is_directory(Error)) {
            if (Error) {
                // todo: print error
                Success = false;
                continue;
            }
            Directories.push_back(DirItem.path().u8string());
        } else if (DirItem.is_regular_file(Error)) {
            if (Error) {
                // todo: print error
                Success = false;
                continue;
            }
            Files.push_back(DirItem.path().u8string());
        } else if (DirItem.is_symlink(Error)) {
            if (Error) {
                // todo: print error
                Success = false;
                continue;
            }
            auto LinkPath = fs::read_symlink(DirItem.path(), Error);
            if (Error) {
                // todo: print error
                Success = false;
                continue;
            }
            if (fs::is_directory(LinkPath, Error)) {
                if (Error) {
                    // todo: print error
                    Success = false;
                    continue;
                }
                // todo: handle symlinks to folders specially
            } else if (fs::is_regular_file(LinkPath, Error)) {
                if (Error) {
                    // todo: print error
                    Success = false;
                    continue;
                }
                Files.push_back(LinkPath.u8string());
            }
        }
    }
    return make_tuple(std::move(Files), std::move(Directories), Success);
}

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
        vector<fs::path> Directories;
        vector<fs::path> Files;
        bool DirSuccess;
        auto CurrentDirectory = UnexploredDirs.front();
        UnexploredDirs.pop_front();
        std::tie(Files, Directories, DirSuccess) = ProcessFolder(CurrentDirectory);
        if (Files.size() || Directories.size()) {
            Callback(Files, Directories);
        }
        UnexploredDirs.insert(UnexploredDirs.end(), Directories.begin(), Directories.end());
        if (!DirSuccess) {
            Result = DirSuccess;
        }
    }

    return Result;
}

bool
FindFilesParallel(
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

    bool Success = true;
    vector<fs::path> UnexploredDirs{};
    UnexploredDirs.push_back(RootPath);

    while (!UnexploredDirs.empty()) {
        vector<future<tuple<vector<fs::path>, vector<fs::path>, bool>>> ParallelResults;
        for (auto const& Dir : UnexploredDirs) {
            ParallelResults.push_back(
                async(launch::async, ProcessFolder, Dir));
        }

        vector<fs::path> Directories{};
        vector<fs::path> Files{};
        for (auto& Result : ParallelResults) {
            vector<fs::path> ResultFiles;
            vector<fs::path> ResultDirs;
            bool ResultSuccess;

            tie(ResultFiles, ResultDirs, ResultSuccess) = Result.get();
                Files.insert(Files.end(), ResultFiles.begin(), ResultFiles.end());
                Directories.insert(Directories.end(), ResultDirs.begin(), ResultDirs.end());
            if (!ResultSuccess) {
                Success = ResultSuccess;
            }
        }

        Callback(Files, Directories);
        UnexploredDirs.swap(Directories);
    }
    return Success;
}

#include "qsync.h"

#include <deque>
#include <future>

using namespace std;
namespace fs = std::filesystem;

const auto FileChunkSize = 100;

SerializedFileInfo
DirItemToFileInfo(
    fs::directory_entry DirItem,
    FileInfo::Type Type,
    fs::path LinkPath = "")
{
    capnp::MallocMessageBuilder Message;
    error_code Error;
    auto Builder = Message.initRoot<FileInfo>();
    Builder.setType(Type);
    auto FileSize = DirItem.file_size(Error);
    if (Error) {
        // Probably should just error out here
        return {};
    }
    Builder.setSize(FileSize);
    auto FileTime = DirItem.last_write_time(Error);
    if (Error) {
        // Use the minimum time in case of error?
        FileTime = fs::file_time_type::min();
    }
    Builder.setModifiedTime(
        chrono::file_clock::to_utc(FileTime).time_since_epoch().count());
    auto Path = DirItem.path().u8string();
    Builder.setPath((const char*)Path.data());
    // Builder.initPath((unsigned int)Path.size());                         // doesn't work
    // memcpy((char*)Builder.getPath().cStr(), Path.c_str(), Path.size());  // doesn't work
    if (Type == FileInfo::Type::SYMLINK && LinkPath != "") {
        auto LinkPathStr = LinkPath.u8string();
        Builder.setLinkPath(LinkPathStr);
        // Builder.initLinkPath((unsigned int)LinkPathStr.size());                                  // doesn't work
        // memcpy((char*)Builder.getLinkPath().cStr(), LinkPathStr.c_str(), LinkPathStr.size());    // doesn't work
    }
    // auto bytes = capnp::writeDataStruct(Builder);

    // works
    SerializedFileInfo Data;
    Data.reserve(Message.sizeInWords() * sizeof(capnp::word));
    VectorStream Stream(Data);
    capnp::writePackedMessage(Stream, Message);
    return std::move(Data);

    // works
    // auto words = capnp::messageToFlatArray(Message);
    // return std::move(words);
}

tuple<kj::Vector<SerializedFileInfo>, vector<fs::path>, bool>
ProcessFolder(
    const fs::path& Parent)
{
    bool Success = true;
    error_code Error;
    kj::Vector<SerializedFileInfo> Files{};
    vector<fs::path> Directories{};
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
            Directories.push_back(DirItem.path());
            Files.add(DirItemToFileInfo(DirItem, FileInfo::Type::DIR));
        } else if (DirItem.is_regular_file(Error)) {
            if (Error) {
                // todo: print error
                Success = false;
                continue;
            }
            Files.add(DirItemToFileInfo(DirItem, FileInfo::Type::FILE));
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
                // Do we traverse symlink directories?
                Files.add(DirItemToFileInfo(DirItem, FileInfo::Type::SYMLINK, LinkPath));
            } else if (fs::is_regular_file(LinkPath, Error)) {
                if (Error) {
                    // todo: print error
                    Success = false;
                    continue;
                }
                Files.add(DirItemToFileInfo(DirItem, FileInfo::Type::SYMLINK, LinkPath));
            }
        }
    }
    return make_tuple(std::move(Files), std::move(Directories), Success);
}

bool
FindFiles(
    const string& Root,
    std::function<FileResultsCallback> Callback)
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
        kj::Vector<SerializedFileInfo> Files;
        bool DirSuccess;
        auto CurrentDirectory = UnexploredDirs.front();
        UnexploredDirs.pop_front();
        std::tie(Files, Directories, DirSuccess) = ProcessFolder(CurrentDirectory);
        if (Files.size()) {
            Callback(Files);
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
        vector<future<tuple<kj::Vector<SerializedFileInfo>, vector<fs::path>, bool>>> ParallelResults;
        for (auto const& Dir : UnexploredDirs) {
            ParallelResults.push_back(
                async(launch::async, ProcessFolder, Dir));
        }

        vector<fs::path> Directories{};
        kj::Vector<SerializedFileInfo> Files{};
        for (auto& Result : ParallelResults) {
            kj::Vector<SerializedFileInfo> ResultFiles;
            vector<fs::path> ResultDirs;
            bool ResultSuccess;

            tie(ResultFiles, ResultDirs, ResultSuccess) = Result.get();
                Files.addAll(ResultFiles.begin(), ResultFiles.end());
                Directories.insert(Directories.end(), ResultDirs.begin(), ResultDirs.end());
            if (!ResultSuccess) {
                Success = ResultSuccess;
            }
        }

        Callback(Files);
        UnexploredDirs.swap(Directories);
    }
    return Success;
}

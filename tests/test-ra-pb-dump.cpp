// Copyright © 2024 chargebyte GmbH
// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <algorithm>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct ProcessResult {
    int exit_code = -1;
    std::string stdout_output;
    std::string stderr_output;
    bool exited = false;
};

std::string ReadFile(const fs::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    EXPECT_TRUE(stream.is_open()) << "Failed to open " << path;

    std::ostringstream content;
    content << stream.rdbuf();

    return content.str();
}

std::string ReadFromFd(int fd)
{
    std::string output;
    char buffer[4096];

    while (true) {
        const ssize_t bytes_read = read(fd, buffer, sizeof(buffer));

        if (bytes_read == 0)
            break;

        if (bytes_read < 0) {
            if (errno == EINTR)
                continue;

            ADD_FAILURE() << "read() failed: " << std::strerror(errno);
            break;
        }

        output.append(buffer, static_cast<size_t>(bytes_read));
    }

    return output;
}

std::string StripTrailingNewlines(std::string text)
{
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
        text.pop_back();

    return text;
}

ProcessResult RunDump(const fs::path &binary, const fs::path &fixture)
{
    int stdout_pipe[2];
    int stderr_pipe[2];
    ProcessResult result;

    if (pipe(stdout_pipe) != 0) {
        ADD_FAILURE() << "pipe(stdout) failed: " << std::strerror(errno);
        return result;
    }

    if (pipe(stderr_pipe) != 0) {
        ADD_FAILURE() << "pipe(stderr) failed: " << std::strerror(errno);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        return result;
    }

    const pid_t pid = fork();
    if (pid == -1) {
        ADD_FAILURE() << "fork() failed: " << std::strerror(errno);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        return result;
    }

    if (pid == 0) {
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

        if (dup2(stdout_pipe[1], STDOUT_FILENO) < 0 || dup2(stderr_pipe[1], STDERR_FILENO) < 0)
            _exit(127);

        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        const std::string binary_string = binary.string();
        const std::string fixture_string = fixture.string();
        char *const argv[] = {
            const_cast<char *>(binary_string.c_str()),
            const_cast<char *>(fixture_string.c_str()),
            nullptr,
        };

        execv(binary_string.c_str(), argv);
        _exit(127);
    }

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    result.stdout_output = ReadFromFd(stdout_pipe[0]);
    result.stderr_output = ReadFromFd(stderr_pipe[0]);

    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) != pid) {
        ADD_FAILURE() << "waitpid() failed: " << std::strerror(errno);
        return result;
    }

    if (WIFEXITED(status)) {
        result.exited = true;
        result.exit_code = WEXITSTATUS(status);
    }

    return result;
}

TEST(RaPbDumpTest, FixturesMatchExpectedYaml)
{
    const fs::path fixture_dir(RA_PB_DUMP_FIXTURE_DIR);
    const fs::path binary(RA_PB_DUMP_PATH);

    ASSERT_TRUE(fs::exists(binary)) << "Missing ra-pb-dump binary at " << binary;
    ASSERT_TRUE(fs::is_directory(fixture_dir)) << "Missing fixture directory " << fixture_dir;

    std::vector<fs::path> fixtures;
    for (const auto &entry : fs::directory_iterator(fixture_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".bin")
            fixtures.push_back(entry.path());
    }

    ASSERT_FALSE(fixtures.empty()) << "No .bin fixtures found in " << fixture_dir;

    std::sort(fixtures.begin(), fixtures.end());

    for (const auto &fixture : fixtures) {
        const fs::path expected_yaml = fixture.parent_path() / (fixture.stem().string() + ".yaml");
        SCOPED_TRACE(fixture.string());

        ASSERT_TRUE(fs::exists(expected_yaml)) << "Missing expected YAML fixture " << expected_yaml;

        const ProcessResult result = RunDump(binary, fixture);
        const std::string expected_output = ReadFile(expected_yaml);

        ASSERT_TRUE(result.exited) << "ra-pb-dump did not exit normally";
        EXPECT_EQ(result.exit_code, EXIT_SUCCESS) << result.stderr_output;
        EXPECT_EQ(StripTrailingNewlines(result.stdout_output), StripTrailingNewlines(expected_output))
            << result.stderr_output;
    }
}

}  // namespace

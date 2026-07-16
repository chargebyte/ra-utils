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

extern "C" {
#include "param_block.h"
}

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

ProcessResult RunProcess(const std::vector<std::string> &arguments)
{
    int stdout_pipe[2];
    int stderr_pipe[2];
    ProcessResult result;

    if (arguments.empty()) {
        ADD_FAILURE() << "No command arguments supplied";
        return result;
    }

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

        std::vector<char *> argv;
        argv.reserve(arguments.size() + 1);
        for (const auto &argument : arguments)
            argv.push_back(const_cast<char *>(argument.c_str()));
        argv.push_back(nullptr);

        execv(arguments.front().c_str(), argv.data());
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

ProcessResult RunDump(const fs::path &binary, const fs::path &fixture)
{
    return RunProcess({binary.string(), fixture.string()});
}

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        char path_template[] = "/tmp/ra-pb-dump-test-XXXXXX";
        const char *created = mkdtemp(path_template);

        EXPECT_NE(created, nullptr) << std::strerror(errno);
        if (created)
            path_ = created;
    }

    ~TemporaryDirectory()
    {
        if (!path_.empty()) {
            std::error_code ec;
            fs::remove_all(path_, ec);
        }
    }

    const fs::path &path() const
    {
        return path_;
    }

private:
    fs::path path_;
};

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

TEST(RaPbDumpTest, DumpsNoInletAsCanonicalNone)
{
    const fs::path binary(RA_PB_DUMP_PATH);
    TemporaryDirectory temp_dir;
    const fs::path fixture = temp_dir.path() / "no-inlet.bin";
    struct param_block_v3 pb = {};

    ASSERT_TRUE(fs::exists(binary)) << "Missing ra-pb-dump binary at " << binary;

    pb_init_v3(&pb);
    pb_refresh_crc_v3(&pb);

    FILE *file = std::fopen(fixture.c_str(), "wb");
    ASSERT_NE(file, nullptr) << std::strerror(errno);
    ASSERT_EQ(std::fwrite(&pb, sizeof(pb), 1, file), 1U);
    ASSERT_EQ(std::fclose(file), 0);

    const ProcessResult result = RunDump(binary, fixture);

    ASSERT_TRUE(result.exited);
    EXPECT_EQ(result.exit_code, EXIT_SUCCESS) << result.stderr_output;
    EXPECT_NE(result.stdout_output.find("inlet: none\n"), std::string::npos);
}

TEST(RaPbDumpTest, SuppressesStoredInletValuesWhenTypeIsNone)
{
    const fs::path binary(RA_PB_DUMP_PATH);
    TemporaryDirectory temp_dir;
    const fs::path fixture = temp_dir.path() / "inlet-none-extra.bin";
    struct param_block_v3 pb = {};

    ASSERT_TRUE(fs::exists(binary)) << "Missing ra-pb-dump binary at " << binary;

    pb_init_v3(&pb);
    pb.inlet_type = INLET_NONE;
    pb.inlet_close_time = 10;
    pb.inlet_open_time = 20;
    pb.inlet_feedback_open_valid_min_mv = htole16(2200);
    pb.inlet_feedback_open_valid_max_mv = htole16(2800);
    pb.inlet_feedback_closed_valid_min_mv = htole16(1700);
    pb.inlet_feedback_closed_valid_max_mv = htole16(2000);
    pb_refresh_crc_v3(&pb);

    FILE *file = std::fopen(fixture.c_str(), "wb");
    ASSERT_NE(file, nullptr) << std::strerror(errno);
    ASSERT_EQ(std::fwrite(&pb, sizeof(pb), 1, file), 1U);
    ASSERT_EQ(std::fclose(file), 0);

    const ProcessResult result = RunDump(binary, fixture);

    ASSERT_TRUE(result.exited);
    EXPECT_EQ(result.exit_code, EXIT_SUCCESS) << result.stderr_output;
    EXPECT_NE(result.stdout_output.find("inlet: none\n"), std::string::npos);
    EXPECT_EQ(result.stdout_output.find("feedback-open-voltage-min"), std::string::npos);
    EXPECT_EQ(result.stdout_output.find("close-time"), std::string::npos);
}

TEST(RaPbDumpTest, DumpsConfiguredInletModes)
{
    const fs::path binary(RA_PB_DUMP_PATH);
    TemporaryDirectory temp_dir;
    const fs::path without_feedback_fixture = temp_dir.path() / "without-feedback.bin";
    const fs::path with_feedback_fixture = temp_dir.path() / "with-feedback.bin";
    struct param_block_v3 pb = {};

    ASSERT_TRUE(fs::exists(binary)) << "Missing ra-pb-dump binary at " << binary;

    pb_init_v3(&pb);
    pb.inlet_type = INLET_WITHOUT_FEEDBACK;
    pb.inlet_close_time = 10;
    pb.inlet_open_time = 11;
    pb_refresh_crc_v3(&pb);

    FILE *file = std::fopen(without_feedback_fixture.c_str(), "wb");
    ASSERT_NE(file, nullptr) << std::strerror(errno);
    ASSERT_EQ(std::fwrite(&pb, sizeof(pb), 1, file), 1U);
    ASSERT_EQ(std::fclose(file), 0);

    const ProcessResult without_feedback_result = RunDump(binary, without_feedback_fixture);

    ASSERT_TRUE(without_feedback_result.exited);
    EXPECT_EQ(without_feedback_result.exit_code, EXIT_SUCCESS) << without_feedback_result.stderr_output;
    EXPECT_NE(without_feedback_result.stdout_output.find("inlet:\n  type: without-feedback\n"), std::string::npos);
    EXPECT_NE(without_feedback_result.stdout_output.find("  close-time: 100 ms\n"), std::string::npos);
    EXPECT_NE(without_feedback_result.stdout_output.find("  open-time: 110 ms\n"), std::string::npos);
    EXPECT_EQ(without_feedback_result.stdout_output.find("feedback-open-voltage-min"), std::string::npos);

    pb_init_v3(&pb);
    pb.inlet_type = INLET_WITH_FEEDBACK;
    pb.inlet_close_time = 10;
    pb.inlet_open_time = 11;
    pb.inlet_feedback_open_valid_min_mv = htole16(2200);
    pb.inlet_feedback_open_valid_max_mv = htole16(2800);
    pb.inlet_feedback_closed_valid_min_mv = htole16(1700);
    pb.inlet_feedback_closed_valid_max_mv = htole16(2000);
    pb_refresh_crc_v3(&pb);

    file = std::fopen(with_feedback_fixture.c_str(), "wb");
    ASSERT_NE(file, nullptr) << std::strerror(errno);
    ASSERT_EQ(std::fwrite(&pb, sizeof(pb), 1, file), 1U);
    ASSERT_EQ(std::fclose(file), 0);

    const ProcessResult with_feedback_result = RunDump(binary, with_feedback_fixture);

    ASSERT_TRUE(with_feedback_result.exited);
    EXPECT_EQ(with_feedback_result.exit_code, EXIT_SUCCESS) << with_feedback_result.stderr_output;
    EXPECT_NE(with_feedback_result.stdout_output.find("inlet:\n  type: with-feedback\n"), std::string::npos);
    EXPECT_NE(with_feedback_result.stdout_output.find("  feedback-open-voltage-min: 2200 mV\n"), std::string::npos);
    EXPECT_NE(with_feedback_result.stdout_output.find("  feedback-open-voltage-max: 2800 mV\n"), std::string::npos);
    EXPECT_NE(with_feedback_result.stdout_output.find("  feedback-closed-voltage-min: 1700 mV\n"), std::string::npos);
    EXPECT_NE(with_feedback_result.stdout_output.find("  feedback-closed-voltage-max: 2000 mV\n"), std::string::npos);
}

TEST(RaPbDumpTest, HelpPrintsUsageAndExitsSuccessfully)
{
    const fs::path binary(RA_PB_DUMP_PATH);
    ASSERT_TRUE(fs::exists(binary)) << "Missing ra-pb-dump binary at " << binary;

    const ProcessResult result = RunProcess({binary.string(), "--help"});

    ASSERT_TRUE(result.exited);
    EXPECT_EQ(result.exit_code, EXIT_SUCCESS);
    EXPECT_NE(result.stderr_output.find("Usage:"), std::string::npos);
    EXPECT_NE(result.stderr_output.find("Options:"), std::string::npos);
}

TEST(RaPbDumpTest, VersionPrintsBannerAndExitsSuccessfully)
{
    const fs::path binary(RA_PB_DUMP_PATH);
    ASSERT_TRUE(fs::exists(binary)) << "Missing ra-pb-dump binary at " << binary;

    const ProcessResult result = RunProcess({binary.string(), "--version"});

    ASSERT_TRUE(result.exited);
    EXPECT_EQ(result.exit_code, EXIT_SUCCESS);
    EXPECT_NE(result.stdout_output.find(binary.string()), std::string::npos);
    EXPECT_NE(result.stdout_output.find("ra-pb-dump"), std::string::npos);
}

TEST(RaPbDumpTest, MissingFileReturnsFailureAndPrintsOpenError)
{
    const fs::path binary(RA_PB_DUMP_PATH);
    ASSERT_TRUE(fs::exists(binary)) << "Missing ra-pb-dump binary at " << binary;

    const fs::path missing = fs::path(RA_PB_DUMP_FIXTURE_DIR) / "does-not-exist.bin";
    const ProcessResult result = RunProcess({binary.string(), missing.string()});

    ASSERT_TRUE(result.exited);
    EXPECT_EQ(result.exit_code, EXIT_FAILURE);
    EXPECT_NE(result.stderr_output.find("cannot open"), std::string::npos);
    EXPECT_NE(result.stderr_output.find(missing.string()), std::string::npos);
}

TEST(RaPbDumpTest, InvalidInputReturnsFailureAndPrintsMagicError)
{
    const fs::path binary(RA_PB_DUMP_PATH);
    const fs::path invalid = fs::path(RA_PB_DUMP_FIXTURE_DIR) / "test-v1-001.yaml";
    ASSERT_TRUE(fs::exists(binary)) << "Missing ra-pb-dump binary at " << binary;
    ASSERT_TRUE(fs::exists(invalid)) << "Missing invalid-input fixture " << invalid;

    const ProcessResult result = RunProcess({binary.string(), invalid.string()});

    ASSERT_TRUE(result.exited);
    EXPECT_EQ(result.exit_code, EXIT_FAILURE);
    EXPECT_NE(result.stderr_output.find("does not look like a parameter block"), std::string::npos);
}

}  // namespace

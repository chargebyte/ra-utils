// Copyright © 2026 chargebyte GmbH
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

std::string ReadFile(const fs::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    EXPECT_TRUE(stream.is_open()) << "Failed to open " << path;

    std::ostringstream content;
    content << stream.rdbuf();

    return content.str();
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

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        char path_template[] = "/tmp/ra-pb-create-test-XXXXXX";
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

void WriteFile(const fs::path &path, const std::string &content)
{
    std::ofstream stream(path, std::ios::binary);
    ASSERT_TRUE(stream.is_open()) << "Failed to open " << path;
    stream << content;
    ASSERT_TRUE(stream.good()) << "Failed to write " << path;
}

ProcessResult RunCreate(const std::vector<std::string> &extra_arguments,
                        const fs::path &input_path,
                        const fs::path &output_path)
{
    std::vector<std::string> arguments = {
        RA_PB_CREATE_PATH,
        "--infile", input_path.string(),
        "--outfile", output_path.string(),
    };

    arguments.insert(arguments.end(), extra_arguments.begin(), extra_arguments.end());

    return RunProcess(arguments);
}

ProcessResult RunDump(const fs::path &input_path)
{
    return RunProcess({RA_PB_DUMP_PATH, input_path.string()});
}

struct param_block ReadParamBlockOrFail(const fs::path &path)
{
    FILE *file = std::fopen(path.c_str(), "rb");
    struct param_block param_block = {};

    EXPECT_NE(file, nullptr) << "Failed to open " << path << ": " << std::strerror(errno);
    if (!file)
        return param_block;

    EXPECT_EQ(pb_read(file, &param_block), 0) << "Failed to read parameter block from " << path;
    EXPECT_EQ(std::fclose(file), 0);

    return param_block;
}

const char kYamlWithoutVersion[] =
    "pt1000s:\n"
    "  - disabled\n"
    "  - disabled\n"
    "  - disabled\n"
    "  - disabled\n"
    "\n"
    "contactors:\n"
    "  - disabled\n"
    "  - disabled\n"
    "  - disabled\n"
    "\n"
    "estops:\n"
    "  - disabled\n"
    "  - disabled\n"
    "  - disabled\n";

const char kYamlWithoutVersionAndInletNoneScalar[] =
    "pt1000s:\n"
    "  - disabled\n"
    "  - disabled\n"
    "  - disabled\n"
    "  - disabled\n"
    "\n"
    "contactors:\n"
    "  - disabled\n"
    "  - disabled\n"
    "  - disabled\n"
    "\n"
    "estops:\n"
    "  - disabled\n"
    "  - disabled\n"
    "  - disabled\n"
    "\n"
    "pluglock: none\n";

const char kYamlWithoutVersionAndInletDisableScalar[] =
    "pt1000s:\n"
    "  - disabled\n"
    "  - disabled\n"
    "  - disabled\n"
    "  - disabled\n"
    "\n"
    "contactors:\n"
    "  - disabled\n"
    "  - disabled\n"
    "  - disabled\n"
    "\n"
    "estops:\n"
    "  - disabled\n"
    "  - disabled\n"
    "  - disabled\n"
    "\n"
    "pluglock: disable\n";

const char kYamlWithoutVersionAndInletDisabledScalar[] =
    "pt1000s:\n"
    "  - disabled\n"
    "  - disabled\n"
    "  - disabled\n"
    "  - disabled\n"
    "\n"
    "contactors:\n"
    "  - disabled\n"
    "  - disabled\n"
    "  - disabled\n"
    "\n"
    "estops:\n"
    "  - disabled\n"
    "  - disabled\n"
    "  - disabled\n"
    "\n"
    "pluglock: disabled\n";

const char kYamlWithoutVersionAndInletTypeNone[] =
    "pt1000s:\n"
    "  - disabled\n"
    "  - disabled\n"
    "  - disabled\n"
    "  - disabled\n"
    "\n"
    "contactors:\n"
    "  - disabled\n"
    "  - disabled\n"
    "  - disabled\n"
    "\n"
    "estops:\n"
    "  - disabled\n"
    "  - disabled\n"
    "  - disabled\n"
    "\n"
    "pluglock:\n"
    "  type: none\n";

std::vector<fs::path> CollectBinFixtures()
{
    const fs::path fixture_dir(RA_PB_CREATE_FIXTURE_DIR);
    std::vector<fs::path> fixtures;

    for (const auto &entry : fs::directory_iterator(fixture_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".bin")
            fixtures.push_back(entry.path());
    }

    std::sort(fixtures.begin(), fixtures.end());
    return fixtures;
}

TEST(RaPbCreateTest, HelpPrintsNewOptions)
{
    const ProcessResult result = RunProcess({RA_PB_CREATE_PATH, "--help"});

    ASSERT_TRUE(result.exited);
    EXPECT_EQ(result.exit_code, EXIT_SUCCESS);
    EXPECT_NE(result.stderr_output.find("--version-from-yaml"), std::string::npos);
    EXPECT_NE(result.stderr_output.find("--version-override"), std::string::npos);
    EXPECT_EQ(result.stderr_output.find("--version-as-requested"), std::string::npos);
}

TEST(RaPbCreateTest, DefaultOutputUsesLatestSupportedVersion)
{
    TemporaryDirectory temp_dir;
    const fs::path input = temp_dir.path() / "input.yaml";
    const fs::path output = temp_dir.path() / "output.bin";

    WriteFile(input, kYamlWithoutVersion);

    const ProcessResult result = RunCreate({}, input, output);

    ASSERT_TRUE(result.exited);
    EXPECT_EQ(result.exit_code, EXIT_SUCCESS) << result.stderr_output;

    const struct param_block param_block = ReadParamBlockOrFail(output);
    EXPECT_EQ(param_block.version, PB_VERSION_V3);
}

TEST(RaPbCreateTest, MissingInletDefaultsToNoInlet)
{
    TemporaryDirectory temp_dir;
    const fs::path input = temp_dir.path() / "input.yaml";
    const fs::path output = temp_dir.path() / "output.bin";

    WriteFile(input, kYamlWithoutVersion);

    const ProcessResult result = RunCreate({}, input, output);

    ASSERT_TRUE(result.exited);
    EXPECT_EQ(result.exit_code, EXIT_SUCCESS) << result.stderr_output;

    const struct param_block param_block = ReadParamBlockOrFail(output);
    EXPECT_EQ(param_block.data.v3.inlet_type, INLET_NONE);
    EXPECT_EQ(param_block.data.v3.inlet_open_time, 0);
    EXPECT_EQ(param_block.data.v3.inlet_close_time, 0);
    EXPECT_EQ(le16toh(param_block.data.v3.inlet_feedback_open_valid_min_mv), 0);
    EXPECT_EQ(le16toh(param_block.data.v3.inlet_feedback_open_valid_max_mv), 0);
    EXPECT_EQ(le16toh(param_block.data.v3.inlet_feedback_closed_valid_min_mv), 0);
    EXPECT_EQ(le16toh(param_block.data.v3.inlet_feedback_closed_valid_max_mv), 0);
}

TEST(RaPbCreateTest, InletDisabledAliasesAndTypeNoneProduceIdenticalBinary)
{
    TemporaryDirectory temp_dir;
    const fs::path input_none = temp_dir.path() / "input-none.yaml";
    const fs::path input_disable = temp_dir.path() / "input-disable.yaml";
    const fs::path input_disabled = temp_dir.path() / "input-disabled.yaml";
    const fs::path input_type_none = temp_dir.path() / "input-type-none.yaml";
    const fs::path output_none = temp_dir.path() / "output-none.bin";
    const fs::path output_disable = temp_dir.path() / "output-disable.bin";
    const fs::path output_disabled = temp_dir.path() / "output-disabled.bin";
    const fs::path output_type_none = temp_dir.path() / "output-type-none.bin";

    WriteFile(input_none, kYamlWithoutVersionAndInletNoneScalar);
    WriteFile(input_disable, kYamlWithoutVersionAndInletDisableScalar);
    WriteFile(input_disabled, kYamlWithoutVersionAndInletDisabledScalar);
    WriteFile(input_type_none, kYamlWithoutVersionAndInletTypeNone);

    const ProcessResult result_none = RunCreate({}, input_none, output_none);
    const ProcessResult result_disable = RunCreate({}, input_disable, output_disable);
    const ProcessResult result_disabled = RunCreate({}, input_disabled, output_disabled);
    const ProcessResult result_type_none = RunCreate({}, input_type_none, output_type_none);

    ASSERT_TRUE(result_none.exited);
    ASSERT_TRUE(result_disable.exited);
    ASSERT_TRUE(result_disabled.exited);
    ASSERT_TRUE(result_type_none.exited);
    EXPECT_EQ(result_none.exit_code, EXIT_SUCCESS) << result_none.stderr_output;
    EXPECT_EQ(result_disable.exit_code, EXIT_SUCCESS) << result_disable.stderr_output;
    EXPECT_EQ(result_disabled.exit_code, EXIT_SUCCESS) << result_disabled.stderr_output;
    EXPECT_EQ(result_type_none.exit_code, EXIT_SUCCESS) << result_type_none.stderr_output;
    EXPECT_EQ(ReadFile(output_none), ReadFile(output_disable));
    EXPECT_EQ(ReadFile(output_none), ReadFile(output_disabled));
    EXPECT_EQ(ReadFile(output_none), ReadFile(output_type_none));
}

TEST(RaPbCreateTest, InletSequenceIsRejected)
{
    TemporaryDirectory temp_dir;
    const fs::path input = temp_dir.path() / "input.yaml";
    const fs::path output = temp_dir.path() / "output.bin";

    WriteFile(input,
              "pt1000s:\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "\n"
              "contactors:\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "\n"
              "estops:\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "\n"
              "pluglock:\n"
              "  - none\n");

    const ProcessResult result = RunCreate({}, input, output);

    ASSERT_TRUE(result.exited);
    EXPECT_EQ(result.exit_code, EXIT_FAILURE);
    EXPECT_NE(result.stderr_output.find("sequences are not allowed"), std::string::npos);
}

TEST(RaPbCreateTest, InletScalarRejectsNonDisabledValues)
{
    TemporaryDirectory temp_dir;
    const fs::path input = temp_dir.path() / "input.yaml";
    const fs::path output = temp_dir.path() / "output.bin";

    WriteFile(input,
              "pt1000s:\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "\n"
              "contactors:\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "\n"
              "estops:\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "\n"
              "pluglock: without-feedback\n");

    const ProcessResult result = RunCreate({}, input, output);

    ASSERT_TRUE(result.exited);
    EXPECT_EQ(result.exit_code, EXIT_FAILURE);
    EXPECT_NE(result.stderr_output.find("expected a pluglock disabled flag"), std::string::npos);
}

TEST(RaPbCreateTest, InletNoneAcceptsAndStoresAdditionalFields)
{
    TemporaryDirectory temp_dir;
    const fs::path input = temp_dir.path() / "input.yaml";
    const fs::path output = temp_dir.path() / "output.bin";

    WriteFile(input,
              "pt1000s:\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "\n"
              "contactors:\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "\n"
              "estops:\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "\n"
              "pluglock:\n"
              "  type: none\n"
              "  close-time: 100 ms\n"
              "  open-time: 200 ms\n"
              "  feedback-open-voltage-min: 2200 mV\n"
              "  feedback-open-voltage-max: 2800 mV\n"
              "  feedback-closed-voltage-min: 1700 mV\n"
              "  feedback-closed-voltage-max: 2000 mV\n");

    const ProcessResult result = RunCreate({}, input, output);

    ASSERT_TRUE(result.exited);
    EXPECT_EQ(result.exit_code, EXIT_SUCCESS) << result.stderr_output;

    const struct param_block param_block = ReadParamBlockOrFail(output);
    EXPECT_EQ(param_block.data.v3.inlet_type, INLET_NONE);
    EXPECT_EQ(param_block.data.v3.inlet_close_time, 10);
    EXPECT_EQ(param_block.data.v3.inlet_open_time, 20);
    EXPECT_EQ(le16toh(param_block.data.v3.inlet_feedback_open_valid_min_mv), 2200);
    EXPECT_EQ(le16toh(param_block.data.v3.inlet_feedback_open_valid_max_mv), 2800);
    EXPECT_EQ(le16toh(param_block.data.v3.inlet_feedback_closed_valid_min_mv), 1700);
    EXPECT_EQ(le16toh(param_block.data.v3.inlet_feedback_closed_valid_max_mv), 2000);
}

TEST(RaPbCreateTest, InletWithoutFeedbackRequiresTimes)
{
    TemporaryDirectory temp_dir;
    const fs::path input = temp_dir.path() / "input.yaml";
    const fs::path output = temp_dir.path() / "output.bin";

    WriteFile(input,
              "pt1000s:\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "\n"
              "contactors:\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "\n"
              "estops:\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "\n"
              "pluglock:\n"
              "  type: without-feedback\n");

    const ProcessResult result = RunCreate({}, input, output);

    ASSERT_TRUE(result.exited);
    EXPECT_EQ(result.exit_code, EXIT_FAILURE);
    EXPECT_NE(result.stderr_output.find("invalid pluglock timing"), std::string::npos);
}

TEST(RaPbCreateTest, InletWithoutFeedbackDefaultsThresholdsToZero)
{
    TemporaryDirectory temp_dir;
    const fs::path input = temp_dir.path() / "input.yaml";
    const fs::path output = temp_dir.path() / "output.bin";

    WriteFile(input,
              "pt1000s:\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "\n"
              "contactors:\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "\n"
              "estops:\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "\n"
              "pluglock:\n"
              "  type: without-feedback\n"
              "  close-time: 100 ms\n"
              "  open-time: 110 ms\n");

    const ProcessResult result = RunCreate({}, input, output);

    ASSERT_TRUE(result.exited);
    EXPECT_EQ(result.exit_code, EXIT_SUCCESS) << result.stderr_output;

    const struct param_block param_block = ReadParamBlockOrFail(output);
    EXPECT_EQ(param_block.data.v3.inlet_type, INLET_WITHOUT_FEEDBACK);
    EXPECT_EQ(param_block.data.v3.inlet_close_time, 10);
    EXPECT_EQ(param_block.data.v3.inlet_open_time, 11);
    EXPECT_EQ(le16toh(param_block.data.v3.inlet_feedback_open_valid_min_mv), 0);
    EXPECT_EQ(le16toh(param_block.data.v3.inlet_feedback_open_valid_max_mv), 0);
    EXPECT_EQ(le16toh(param_block.data.v3.inlet_feedback_closed_valid_min_mv), 0);
    EXPECT_EQ(le16toh(param_block.data.v3.inlet_feedback_closed_valid_max_mv), 0);
}

TEST(RaPbCreateTest, InletWithFeedbackRequiresAllThresholds)
{
    TemporaryDirectory temp_dir;
    const fs::path input = temp_dir.path() / "input.yaml";
    const fs::path output = temp_dir.path() / "output.bin";

    WriteFile(input,
              "pt1000s:\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "\n"
              "contactors:\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "\n"
              "estops:\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "\n"
              "pluglock:\n"
              "  type: with-feedback\n"
              "  close-time: 100 ms\n"
              "  open-time: 110 ms\n"
              "  feedback-open-voltage-min: 2200 mV\n");

    const ProcessResult result = RunCreate({}, input, output);

    ASSERT_TRUE(result.exited);
    EXPECT_EQ(result.exit_code, EXIT_FAILURE);
    EXPECT_NE(result.stderr_output.find("all four feedback voltages are required"), std::string::npos);
}

TEST(RaPbCreateTest, InletWithFeedbackStoresConfiguredValues)
{
    TemporaryDirectory temp_dir;
    const fs::path input = temp_dir.path() / "input.yaml";
    const fs::path output = temp_dir.path() / "output.bin";

    WriteFile(input,
              "pt1000s:\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "\n"
              "contactors:\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "\n"
              "estops:\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "\n"
              "pluglock:\n"
              "  type: with-feedback\n"
              "  close-time: 100 ms\n"
              "  open-time: 110 ms\n"
              "  feedback-open-voltage-min: 2200 mV\n"
              "  feedback-open-voltage-max: 2800 mV\n"
              "  feedback-closed-voltage-min: 1700 mV\n"
              "  feedback-closed-voltage-max: 2000 mV\n");

    const ProcessResult result = RunCreate({}, input, output);

    ASSERT_TRUE(result.exited);
    EXPECT_EQ(result.exit_code, EXIT_SUCCESS) << result.stderr_output;

    const struct param_block param_block = ReadParamBlockOrFail(output);
    EXPECT_EQ(param_block.data.v3.inlet_type, INLET_WITH_FEEDBACK);
    EXPECT_EQ(param_block.data.v3.inlet_close_time, 10);
    EXPECT_EQ(param_block.data.v3.inlet_open_time, 11);
    EXPECT_EQ(le16toh(param_block.data.v3.inlet_feedback_open_valid_min_mv), 2200);
    EXPECT_EQ(le16toh(param_block.data.v3.inlet_feedback_open_valid_max_mv), 2800);
    EXPECT_EQ(le16toh(param_block.data.v3.inlet_feedback_closed_valid_min_mv), 1700);
    EXPECT_EQ(le16toh(param_block.data.v3.inlet_feedback_closed_valid_max_mv), 2000);
}

TEST(RaPbCreateTest, VersionFromYamlUsesRequestedSupportedVersion)
{
    TemporaryDirectory temp_dir;
    const fs::path input = fs::path(RA_PB_CREATE_FIXTURE_DIR) / "test-v1-001.yaml";
    const fs::path output = temp_dir.path() / "output.bin";

    const ProcessResult result = RunCreate({"--version-from-yaml"}, input, output);

    ASSERT_TRUE(result.exited);
    EXPECT_EQ(result.exit_code, EXIT_SUCCESS) << result.stderr_output;

    const struct param_block param_block = ReadParamBlockOrFail(output);
    EXPECT_EQ(param_block.version, PB_VERSION_V1);
}

TEST(RaPbCreateTest, VersionFromYamlFallsBackToLatestWhenYamlOmitsVersion)
{
    TemporaryDirectory temp_dir;
    const fs::path input = temp_dir.path() / "input.yaml";
    const fs::path output = temp_dir.path() / "output.bin";

    WriteFile(input, kYamlWithoutVersion);

    const ProcessResult result = RunCreate({"--version-from-yaml"}, input, output);

    ASSERT_TRUE(result.exited);
    EXPECT_EQ(result.exit_code, EXIT_SUCCESS) << result.stderr_output;

    const struct param_block param_block = ReadParamBlockOrFail(output);
    EXPECT_EQ(param_block.version, PB_VERSION_V3);
}

TEST(RaPbCreateTest, VersionOverrideSelectsRequestedVersion)
{
    TemporaryDirectory temp_dir;
    const fs::path input = temp_dir.path() / "input.yaml";
    const fs::path output = temp_dir.path() / "output.bin";

    WriteFile(input, kYamlWithoutVersion);

    const ProcessResult result = RunCreate({"--version-override", "1"}, input, output);

    ASSERT_TRUE(result.exited);
    EXPECT_EQ(result.exit_code, EXIT_SUCCESS) << result.stderr_output;

    const struct param_block param_block = ReadParamBlockOrFail(output);
    EXPECT_EQ(param_block.version, PB_VERSION_V1);
}

TEST(RaPbCreateTest, VersionOverrideWinsOverYamlVersionSelection)
{
    TemporaryDirectory temp_dir;
    const fs::path input = fs::path(RA_PB_CREATE_FIXTURE_DIR) / "test-v1-001.yaml";
    const fs::path output = temp_dir.path() / "output.bin";

    const ProcessResult result = RunCreate({"--version-from-yaml", "--version-override", "2"}, input, output);

    ASSERT_TRUE(result.exited);
    EXPECT_EQ(result.exit_code, EXIT_SUCCESS) << result.stderr_output;

    const struct param_block param_block = ReadParamBlockOrFail(output);
    EXPECT_EQ(param_block.version, PB_VERSION_V2);
}

TEST(RaPbCreateTest, InletConfigurationIsDroppedWhenWritingVersion2)
{
    TemporaryDirectory temp_dir;
    const fs::path input = temp_dir.path() / "input.yaml";
    const fs::path output = temp_dir.path() / "output.bin";

    WriteFile(input,
              "pt1000s:\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "\n"
              "contactors:\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "\n"
              "estops:\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "\n"
              "pluglock:\n"
              "  type: with-feedback\n"
              "  close-time: 100 ms\n"
              "  open-time: 110 ms\n"
              "  feedback-open-voltage-min: 2200 mV\n"
              "  feedback-open-voltage-max: 2800 mV\n"
              "  feedback-closed-voltage-min: 1700 mV\n"
              "  feedback-closed-voltage-max: 2000 mV\n");

    const ProcessResult result = RunCreate({"--version-override", "2"}, input, output);

    ASSERT_TRUE(result.exited);
    EXPECT_EQ(result.exit_code, EXIT_SUCCESS) << result.stderr_output;
    EXPECT_NE(result.stderr_output.find("dropping pluglock configuration"), std::string::npos);

    const struct param_block param_block = ReadParamBlockOrFail(output);
    EXPECT_EQ(param_block.version, PB_VERSION_V2);
}

TEST(RaPbCreateTest, RcmMappingCanBeFollowedByInletConfiguration)
{
    TemporaryDirectory temp_dir;
    const fs::path input = temp_dir.path() / "input.yaml";
    const fs::path output = temp_dir.path() / "output.bin";

    WriteFile(input,
              "version: 3\n"
              "\n"
              "pt1000s:\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "\n"
              "contactors:\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "\n"
              "estops:\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "\n"
              "rcm:\n"
              "  fault-polarity: none\n"
              "  test-polarity: none\n"
              "  test-trigger-time: 810 ms\n"
              "  test-check-tripped-time: 810 ms\n"
              "  test-check-normal-time: 410 ms\n"
              "\n"
              "pluglock:\n"
              "  type: without-feedback\n"
              "  close-time: 500 ms\n"
              "  open-time: 500 ms\n");

    const ProcessResult result = RunCreate({}, input, output);

    ASSERT_TRUE(result.exited);
    EXPECT_EQ(result.exit_code, EXIT_SUCCESS) << result.stderr_output;

    const struct param_block param_block = ReadParamBlockOrFail(output);
    EXPECT_EQ(param_block.version, PB_VERSION_V3);
    EXPECT_EQ(param_block.data.v3.inlet_type, INLET_WITHOUT_FEEDBACK);
    EXPECT_EQ(param_block.data.v3.inlet_close_time, 50);
    EXPECT_EQ(param_block.data.v3.inlet_open_time, 50);
}

TEST(RaPbCreateTest, UnsupportedVersionOverrideFails)
{
    TemporaryDirectory temp_dir;
    const fs::path input = temp_dir.path() / "input.yaml";
    const fs::path output = temp_dir.path() / "output.bin";

    WriteFile(input, kYamlWithoutVersion);

    const ProcessResult result = RunCreate({"--version-override", "4"}, input, output);

    ASSERT_TRUE(result.exited);
    EXPECT_EQ(result.exit_code, EXIT_FAILURE);
    EXPECT_NE(result.stderr_output.find("requested parameter block version 4 is not supported"), std::string::npos);
}

TEST(RaPbCreateTest, UnsupportedYamlVersionFailsWhenRequested)
{
    TemporaryDirectory temp_dir;
    const fs::path input = temp_dir.path() / "input.yaml";
    const fs::path output = temp_dir.path() / "output.bin";

    WriteFile(input,
              "version: 4\n"
              "\n"
              "pt1000s:\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "\n"
              "contactors:\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n"
              "\n"
              "estops:\n"
              "  - disabled\n"
              "  - disabled\n"
              "  - disabled\n");

    const ProcessResult result = RunCreate({"--version-from-yaml"}, input, output);

    ASSERT_TRUE(result.exited);
    EXPECT_EQ(result.exit_code, EXIT_FAILURE);
    EXPECT_NE(result.stderr_output.find("requested parameter block version 4 is not supported"), std::string::npos);
}

TEST(RaPbCreateTest, YamlFixturesCreateMatchingBinaryFixtures)
{
    const std::vector<fs::path> fixtures = CollectBinFixtures();
    ASSERT_FALSE(fixtures.empty()) << "No .bin fixtures found in " << RA_PB_CREATE_FIXTURE_DIR;

    for (const auto &bin_fixture : fixtures) {
        TemporaryDirectory temp_dir;
        const fs::path yaml_fixture = bin_fixture.parent_path() / (bin_fixture.stem().string() + ".yaml");
        const fs::path output = temp_dir.path() / bin_fixture.filename();

        SCOPED_TRACE(bin_fixture.string());
        ASSERT_TRUE(fs::exists(yaml_fixture)) << "Missing YAML fixture " << yaml_fixture;

        const ProcessResult result = RunCreate({"--version-from-yaml"}, yaml_fixture, output);

        ASSERT_TRUE(result.exited) << result.stderr_output;
        EXPECT_EQ(result.exit_code, EXIT_SUCCESS) << result.stderr_output;
        EXPECT_EQ(ReadFile(output), ReadFile(bin_fixture)) << result.stderr_output;
    }
}

TEST(RaPbCreateTest, BinaryFixturesRoundTripThroughDumpAndCreate)
{
    const std::vector<fs::path> fixtures = CollectBinFixtures();
    ASSERT_FALSE(fixtures.empty()) << "No .bin fixtures found in " << RA_PB_CREATE_FIXTURE_DIR;

    for (const auto &bin_fixture : fixtures) {
        TemporaryDirectory temp_dir;
        const fs::path yaml_from_dump = temp_dir.path() / (bin_fixture.stem().string() + ".yaml");
        const fs::path output = temp_dir.path() / bin_fixture.filename();

        SCOPED_TRACE(bin_fixture.string());

        const ProcessResult dump_result = RunDump(bin_fixture);
        ASSERT_TRUE(dump_result.exited) << dump_result.stderr_output;
        ASSERT_EQ(dump_result.exit_code, EXIT_SUCCESS) << dump_result.stderr_output;

        WriteFile(yaml_from_dump, dump_result.stdout_output);

        const ProcessResult create_result = RunCreate({"--version-from-yaml"}, yaml_from_dump, output);
        ASSERT_TRUE(create_result.exited) << create_result.stderr_output;
        EXPECT_EQ(create_result.exit_code, EXIT_SUCCESS) << create_result.stderr_output;
        EXPECT_EQ(ReadFile(output), ReadFile(bin_fixture)) << create_result.stderr_output;

        const fs::path expected_yaml = bin_fixture.parent_path() / (bin_fixture.stem().string() + ".yaml");
        ASSERT_TRUE(fs::exists(expected_yaml)) << "Missing YAML fixture " << expected_yaml;
        EXPECT_EQ(StripTrailingNewlines(dump_result.stdout_output), StripTrailingNewlines(ReadFile(expected_yaml)))
            << dump_result.stderr_output;
    }
}

}  // namespace

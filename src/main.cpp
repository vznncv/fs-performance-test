/**
 * Demo project that test sd card speed using FatFS and LittleFS file systems.
 */
#include "FATFileSystem.h"
#include "LittleFileSystem.h"
#include "SDBlockDevice.h"
#include "mbed.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

#include "pathutil.h"

using namespace pathutil;

// function prototypes
void log(const char* fmt_str, ...);
void measure_file_creation_speed(const char* test_dir, bool cleanup_dir = true, size_t file_size = 128, size_t n_files = 1000);
void measure_write_read_speed(const char* test_dir, bool cleanup_dir = true, size_t block_size = 1024, size_t blocks_per_file = 32 * 1024, size_t files_per_round = 1, size_t n_rounds = 8);

#define CHECK_ERROR(EXPR)                                                                         \
    {                                                                                             \
        int code = EXPR;                                                                          \
        if (code) {                                                                               \
            MBED_ERROR(MBED_MAKE_ERROR(MBED_MODULE_APPLICATION, MBED_ERROR_CODE_UNKNOWN), #EXPR); \
        }                                                                                         \
    }

void log(const char* fmt_str, ...)
{
    va_list args;
    va_start(args, fmt_str);
    vprintf(fmt_str, args);
    printf("\n");
    va_end(args);
}

class ProgressMeasurer {
public:
    ProgressMeasurer(const char* unit, size_t total_steps, size_t log_step, float block_size)
        : _unit(unit)
        , _total_steps(total_steps)
        , _log_step(log_step)
        , _block_size(block_size)
        , _log_block_size(block_size * log_step)
    {
    }

    void start()
    {
        _timer.start();
        _current_step = 0;
        _log_i = 0;
        _prev_t = 0;

        log("Progress %4i/%i", _current_step, _total_steps);
    }

    void update()
    {
        _log_i++;
        _current_step++;
        if (_log_i >= _log_step) {
            _log_i = 0;
            float current_t = _timer.read();
            float dt = current_t - _prev_t;
            _prev_t = current_t;
            float speed = _log_block_size / dt;
            log("Progress %4i/%i. Speed %.1f %s", _current_step, _total_steps, speed, _unit);
        }
    }

    void finish()
    {
        float dt;
        float speed;
        float current_t;
        float total_time;

        if (_log_i != 0) {
            current_t = _timer.read();
            dt = current_t - _prev_t;
            speed = _log_block_size / dt;
            log("Progress %4i/%i. Speed %.1f %s", _current_step, _total_steps, speed, _unit);
        }

        total_time = _timer.read();
        speed = _block_size * _total_steps / total_time;
        log("-----------------------------------");
        log("Total time:    %.1f seconds", total_time);
        log("Average speed: %.1f %s", speed, _unit);
        log("-----------------------------------");
    }

private:
    const char* _unit;
    size_t _total_steps;
    size_t _log_step;
    float _block_size;
    float _log_block_size;

    Timer _timer;
    size_t _current_step;
    size_t _log_i;
    float _prev_t;
};

void measure_file_creation_speed(const char* test_dir, bool cleanup_dir, size_t file_size, size_t n_files)
{
    // remove previous directory if it exists
    if (exists(test_dir)) {
        log("Remove existed test directory '%s'...", test_dir);
        CHECK_ERROR(rmtree(test_dir));
        log("Directory has been removed");
    }

    // prepare buffer with content
    uint8_t* buff = new uint8_t[file_size];
    memset(buff, 'A', file_size);
    // prepare base file path
    char file_path[192];
    strcpy(file_path, test_dir);
    normpath(file_path);
    size_t path_len = strlen(file_path);
    file_path[path_len] = '/';
    const char* file_prefix = "test_file_";
    strcpy(file_path + path_len + 1, file_prefix);
    char* file_path_suffix_start = file_path + path_len + 1 + strlen(file_prefix);

    log("Create test directory");
    CHECK_ERROR(makedirs(test_dir, 0777, true));

    // create files
    ProgressMeasurer progress_measurer("it/s", n_files, 32, 1.0);

    log("Start files creation");
    log("  - number of file: %i", n_files);
    log("  - file size:      %i bytes", file_size);
    log("-----------------------------------");

    progress_measurer.start();
    for (int i = 0; i < n_files; i++) {
        sprintf(file_path_suffix_start, "%i", i);
        write_data(file_path, buff, file_size);
        progress_measurer.update();
    }
    progress_measurer.finish();

    log("Remove test directory ...");
    rmtree(test_dir);
    log("Test directory has been removed");
}

void measure_write_read_speed(const char* test_dir, bool cleanup_dir, size_t block_size, size_t blocks_per_file, size_t files_per_round, size_t n_rounds)
{
    // remove previous directory if it exists
    if (exists(test_dir)) {
        log("Remove existed test directory '%s'...", test_dir);
        CHECK_ERROR(rmtree(test_dir));
        log("Directory has been removed");
    }
    log("Create test directory");
    CHECK_ERROR(makedirs(test_dir, 0777, true));
    log("-----------------------------------");

    // prepare buffer with content
    uint8_t* buff = new uint8_t[block_size];
    memset(buff, 'B', block_size);

    const size_t progress_tick = 64;
    size_t written_size;
    size_t read_size;

    FILE** files = new FILE*[files_per_round];
    size_t base_dir_len = strlen(test_dir);
    char path[128];
    char filename[32];
    strcpy(path, test_dir);

    // write test
    log("Start write test");
    ProgressMeasurer write_measurer("B/s", blocks_per_file * n_rounds * files_per_round, progress_tick, block_size);
    write_measurer.start();
    for (size_t round_i = 0; round_i < n_rounds; round_i++) {
        // open test files
        for (size_t file_no = 0; file_no < files_per_round; file_no++) {
            sprintf(filename, "test_%i_%i.txt", round_i, file_no);
            path[base_dir_len] = '\0';
            append_path(path, filename);
            files[file_no] = fopen(path, "wb");
            if (files[file_no] == NULL) {
                MBED_ERROR(MBED_ERROR_OPEN_FAILED, "Fail to create file");
            }
        }

        for (size_t block_i = 0; block_i < blocks_per_file; block_i++) {
            for (size_t file_no = 0; file_no < files_per_round; file_no++) {
                written_size = fwrite(buff, 1, block_size, files[file_no]);
                if (written_size != block_size) {
                    MBED_ERROR(MBED_ERROR_WRITE_FAILED, "Fail to write data to file");
                }
                write_measurer.update();
            }
        }

        // close test files
        for (size_t file_no = 0; file_no < files_per_round; file_no++) {
            if (fclose(files[file_no])) {
                MBED_ERROR(MBED_ERROR_CLOSE_FAILED, "Fail to close file");
            }
        }
    }
    write_measurer.finish();

    // read test
    log("Start read test");
    ProgressMeasurer read_measurer("B/s", blocks_per_file * n_rounds * files_per_round, progress_tick, block_size);
    read_measurer.start();
    for (size_t round_i = 0; round_i < n_rounds; round_i++) {
        // open test files
        for (size_t file_no = 0; file_no < files_per_round; file_no++) {
            sprintf(filename, "test_%i_%i.txt", round_i, file_no);
            path[base_dir_len] = '\0';
            append_path(path, filename);
            files[file_no] = fopen(path, "rb");
            if (files[file_no] == NULL) {
                MBED_ERROR(MBED_ERROR_OPEN_FAILED, "Fail to create file");
            }
        }

        for (size_t block_i = 0; block_i < blocks_per_file; block_i++) {
            for (size_t file_no = 0; file_no < files_per_round; file_no++) {
                read_size = fread(buff, 1, block_size, files[file_no]);
                if (read_size != block_size) {
                    MBED_ERROR(MBED_ERROR_READ_FAILED, "Fail to read data to file");
                }
                read_measurer.update();
            }
        }

        // close test files
        for (size_t file_no = 0; file_no < files_per_round; file_no++) {
            if (fclose(files[file_no])) {
                MBED_ERROR(MBED_ERROR_CLOSE_FAILED, "Fail to close file");
            }
        }
    }
    read_measurer.finish();

    // cleanup data
    delete[] files;
    delete[] buff;

    log("Remove test directory ...");
    rmtree(test_dir);
    log("Test directory has been removed");
}

#define XLIT_STR(a) LIT_STR(a)
#define LIT_STR(s) #s

int main()
{
    // parameters of the file creation test
    const int CFT_FILE_SIZE = 128;
    const int CFT_NUM_FILES = 250;
    // parameter of write/read test
    const int WRT_BLOCK_SIZE = 4 * 1024;
    const int WRT_BLOCKS_PER_FILE = 128;
    const int WRT_FILES_PER_ROUND = 4;
    const int WRT_N_ROUNDS = 2;
    // files systems for tests
    const int FS_NUM = 2;
    FileSystem* fs_ptrs[FS_NUM];
    fs_ptrs[0] = new FATFileSystem("fat");
    fs_ptrs[1] = new LittleFileSystem("ls");
    const char* fs_name[FS_NUM] = { "FatFS", "LittleFS" };

    int err_code;

    // show configuration information
    log("------------------ start --------------------\n");
    log("SD card pins:");
    log("  - MOSI: %s", XLIT_STR(MBED_CONF_APP_SD_MOSI));
    log("  - MISO: %s", XLIT_STR(MBED_CONF_APP_SD_MISO));
    log("  - CLK:  %s", XLIT_STR(MBED_CONF_APP_SD_CLK));
    log("  - CS:   %s", XLIT_STR(MBED_CONF_APP_SD_CS));
    log("SD card SPI frequency: %.2f MHz", MBED_CONF_APP_SD_FREQ / 1000000.0f);
    log("--------------------------------------------");
    log("Warning: all sd card date will be destroyed");
    log("");

    // mount sd card
    SDBlockDevice* sd_ptr = new SDBlockDevice(MBED_CONF_APP_SD_MOSI, MBED_CONF_APP_SD_MISO, MBED_CONF_APP_SD_CLK, MBED_CONF_APP_SD_CS, MBED_CONF_APP_SD_FREQ);
    CHECK_ERROR(sd_ptr->init());
    log("SD card has been initialized.");

    for (int i = 0; i < FS_NUM; i++) {
        log("--------------------------------------------");
        log("Test %s", fs_name[i]);
        FileSystem* fs_ptr = fs_ptrs[i];
        err_code = fs_ptr->mount(sd_ptr);
        if (err_code) {
            log("Format sd card ...");
            CHECK_ERROR(fs_ptr->reformat(sd_ptr));
            log("SD card has been formatted");
        }

        char test_dir_path[128];

        log("Test SD card file creation speed");
        sprintf(test_dir_path, "/%s/cft_demo_dir", fs_ptr->getName());
        measure_file_creation_speed(test_dir_path, true, CFT_FILE_SIZE, CFT_NUM_FILES);

        log("Test SD card write/read speed");
        sprintf(test_dir_path, "/%s/wrt_demo_dir", fs_ptr->getName());
        measure_write_read_speed(test_dir_path, true, WRT_BLOCK_SIZE, WRT_BLOCKS_PER_FILE, WRT_FILES_PER_ROUND, WRT_N_ROUNDS);
        log("Complete");

        CHECK_ERROR(fs_ptr->unmount());
    }
    CHECK_ERROR(sd_ptr->deinit());

    // led demo
    DigitalOut led(LED2);
    while (true) {
        led = !led;
        wait_ms(500);
    }
}

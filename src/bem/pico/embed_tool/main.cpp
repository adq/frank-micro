/*
 * B-em Pico Version (C) 2021 Graham Sanderson
 */
#include <cstdio>
#include <exception>
#include <string>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstring>
#include <vector>
#include <cstdarg>
#include <allegro5/allegro.h>

extern "C" {
#include "sdf.h"
}

#define ERROR_ARGS -1
#define ERROR_UNKNOWN -2
#define ERROR_SYNTAX -3
#define ERROR_FILE -3

using std::string;
using std::vector;
using std::filesystem::path;
using std::tuple;

bool is_disc;
bool is_rom;

static string hex_string(int value, int width = 8, bool prefix = true) {
    std::stringstream ss;
    if (prefix) ss << "0x";
    ss << std::setfill('0') << std::setw(width) << std::hex << value;
    return ss.str();
}

extern "C" void log_warn(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    fputs("WARN  ", stderr);
    vfprintf(stderr, fmt, ap);
    putc('\n', stderr);
    va_end(ap);
}

struct failure : std::exception {
    failure(int code, string s) : c(code), s(std::move(s)) {}

    const char *what() const noexcept override {
        return s.c_str();
    }

    int code() const { return c; }

private:
    int c;
    string s;
};

static int usage() {
    fprintf(stderr, "Usage: embed_tool [-d | -r] <description file> <output c file>\n");
    return ERROR_ARGS;
}

string trim(const string &str) {
    string s = str;
    const string pattern = " \f\n\r\t\v";
    auto n1 = s.find_first_not_of(pattern);
    if (n1 != string::npos) s = s.substr(n1);
    auto n2 = s.find_last_not_of(pattern);
    if (n2 != string::npos) s = s.substr(0, n2+1);
    else if (n1 == string::npos) s = "";
    return s;
}

bool consume_string_and_trim(string& line, string to_match) {
    trim(line);
    if (line.find(to_match) == 0) {
        line = trim(line.substr(to_match.length()));
        return true;
    }
    return false;
}

std::shared_ptr<std::ifstream> open_file(const string& name) {
    std::shared_ptr<std::ifstream> ifile = std::make_shared<std::ifstream>(name);
    if (!ifile->good()) {
        std::stringstream ss;
        ss << "Can't open " << name;
        throw failure(ERROR_FILE, ss.str());
    }
    return ifile;
}

void generate(const string& in_file_name, std::ofstream &out) {
    string entity = is_disc ? "disc" : "rom";

    string line;
    auto dump = [&](vector<uint8_t> data) {
        for (uint i = 0; i < data.size(); i += 32) {
            out << "   ";
            for (uint j = i; j < std::min(i + 32, (uint) data.size()); j++) {
                out << hex_string(data[j], 2) << ", ";
            }
            out << "\n";
        }
    };

    std::vector<string> names;

    out << "#include <stdio.h>\n";
    out << "#include <allegro5/allegro.h>\n";
    out << "#include \"" << entity << "s/" << entity << "s.h\"\n";

    struct file_state {
        explicit file_state(const string& file_name) : file_name(file_name) {
            directory_path = std::filesystem::absolute(file_name).parent_path();
            base_path = directory_path;
            in_file = open_file(file_name);
        }
        string file_name;
        path directory_path;
        path base_path;
        std::shared_ptr<std::ifstream> in_file;
        int line_no = 0;

        bool getline(string& line) {
            line_no++;
            return (bool)std::getline(*in_file, line);
        }

        [[nodiscard]] path resolve_path(const string& p) const {
            path rc(p);
            if (rc.is_relative()) {
                rc = base_path / rc;
            }
            return rc;
        }
    };

    uint default_index = 0;
    vector<std::shared_ptr<file_state>> input_stack;
    input_stack.emplace_back(std::make_shared<file_state>(in_file_name));
    std::shared_ptr<file_state> current_input = input_stack[0];
    try {
        do {
            while (current_input->getline(line)) {
                line = trim(line);
                if (!line.empty()) {
                    if (consume_string_and_trim(line, "#")) {
                        continue;
                    } else if (consume_string_and_trim(line, "!")) {
                        if (consume_string_and_trim(line, "path ")) {
                            current_input->base_path = line;
                            if (current_input->base_path.is_relative()) {
                                current_input->base_path = current_input->directory_path / current_input->base_path;
                            }
                            continue;
                        } else if (consume_string_and_trim(line, "replace ")) {
                            path p = current_input->resolve_path(line);
                            current_input = input_stack[input_stack.size() - 1] = std::make_shared<file_state>(p);
                            continue;
                        } else if (consume_string_and_trim(line, "replace_if ")) {
                            path p = current_input->resolve_path(line);
                            if (std::filesystem::exists(p)) {
                                current_input = input_stack[input_stack.size() - 1] = std::make_shared<file_state>(p);
                            }
                            continue;
                        } else if (consume_string_and_trim(line, "include ")) {
                            path p = current_input->resolve_path(line);
                            input_stack.emplace_back(std::make_shared<file_state>(p));
                            current_input = input_stack[input_stack.size() - 1];
                            continue;
                        } else if (consume_string_and_trim(line, "include_if ")) {
                            path p = current_input->resolve_path(line);
                            if (std::filesystem::exists(p)) {
                                input_stack.emplace_back(std::make_shared<file_state>(p));
                                current_input = input_stack[input_stack.size() - 1];
                            }
                            continue;
                        } else {
                            std::stringstream ss;
                            if (line.find_first_of(' ') != string::npos) {
                                line = line.substr(0, line.find_first_of(' '));
                            }
                            ss << "Unknown directive: " << line;
                            throw failure(ERROR_SYNTAX, ss.str());
                        }
                    } else if (consume_string_and_trim(line, ">")) {
                        default_index = names.size();
                    }
                    size_t split = line.find_first_of('=');
                    if (split == string::npos) {
                        throw failure(ERROR_SYNTAX, "Expected to find name = value");
                    }
                    string name = trim(line.substr(0, split));
                    string file_name = trim(line.substr(split + 1));
                    if (name.empty() || file_name.empty()) {
                        throw failure(ERROR_SYNTAX, "Expected to find name = value");
                    }
                    int index = names.size();
                    names.push_back(name);
                    path file_path = current_input->resolve_path(file_name);
                    FILE *fp;
                    const char *fn = file_path.c_str();
                    const char *ext;
                    if ((ext = strrchr(fn, '.')))
                        ext++;
                    if ((fp = fopen(fn, "rb"))) {
                        const struct sdf_geometry *geo;
                        vector<uint8_t> contents;
                        if (is_disc) {
                            if ((geo = sdf_find_geo(fn, ext, fp))) {
                                out << "static const struct sdf_geometry disc_" << index << "_geometry= {\n";
                                out << "   .name = \"" << geo->name << "\",\n";
                                out << "   .sides = " << (int) geo->sides << ",\n";
                                out << "   .density = " << (int) geo->density << ",\n";
                                out << "   .tracks = " << (int) geo->tracks << ",\n";
                                out << "   .sectors_per_track = " << (int) geo->sectors_per_track << ",\n";
                                out << "   .sector_size = " << (int) geo->sector_size << ",\n";
                                out << "};\n";
                                printf("%s: %s, %s, %d tracks, %s, %d %d byte sectors/track\n",
                                       fn, geo->name, sdf_desc_sides(geo), geo->tracks,
                                       sdf_desc_dens(geo), geo->sectors_per_track, geo->sector_size);
                                vector<uint8_t> blank(geo->sector_size, 0xe5);
                                vector<uint8_t> sector(geo->sector_size);
                                fseek(fp, 0, SEEK_SET);
                                int blank_sectors = 0;
                                int read;
                                do {
                                    read = fread(sector.data(), 1, geo->sector_size, fp);
                                    if (read == 0) break;
                                    sector.resize(read);
                                    if (sector == blank) {
                                        blank_sectors++;
                                    } else {
                                        for (; blank_sectors > 0; blank_sectors--) {
                                            contents.insert(contents.end(), blank.begin(), blank.end());
                                        }
                                        contents.insert(contents.end(), sector.begin(), sector.end());
                                    }
                                } while (read == geo->sector_size);
                            } else {
                                std::stringstream ss;
                                ss << name << ": unknown geometry";
                                throw failure(ERROR_FILE, ss.str());
                            }
                        } else {
                            fseek(fp, 0, SEEK_END);
                            size_t size = ftell(fp);
                            fseek(fp, 0, SEEK_SET);
                            contents.resize(size);
                            if (1 != fread(contents.data(), size, 1, fp)) {
                                throw failure(ERROR_FILE, "Failed to read file\n");
                            }
                        }
                        printf("%s %d\n", name.c_str(), (int) contents.size());
                        fclose(fp);
                        out << "static const uint8_t " << entity << "_" << index << "_data[" << contents.size()
                            << "] = {\n";
                        dump(contents);
                        out << "};\n";
                    } else {
                        std::stringstream ss;
                        ss << "Can't open " << file_path << "\n";
                        throw failure(ERROR_FILE, ss.str());
                    }
                }
            }
            input_stack.pop_back();
            if (input_stack.empty()) {
                current_input = nullptr;
            } else {
                current_input = input_stack[input_stack.size() - 1];
            }
        } while (current_input);
    } catch (failure &f) {
        throw failure(f.code(), current_input->file_name + " line " + std::to_string(current_input->line_no) + " : " + f.what());
    }
    out << "const embedded_" << entity << "_t embedded_" << entity << "s[" << names.size() << "] = {\n";
    int i = 0;
    for (const auto &name : names) {
        out << "   { \"" << name << "\", ";
        if (is_disc) {
            out << "&" << entity << "_" << i << "_geometry, ";
        }
        out << entity << "_" << i << "_data, sizeof(" << entity << "_" << i << "_data) },\n";
        i++;
    }
    out << "};\n";
    out << "const uint embedded_" << entity << "_count = " << names.size() << ";\n";
    out << "const uint embedded_" << entity << "_default = " << default_index << ";\n";
}

int main(int argc, char **argv) {
    string in_filename, out_filename;
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            switch (argv[i][1]) {
                case 'r':
                    is_rom = true;
                    break;
                case 'd':
                    is_disc = true;
                    break;
            }
        } else if (in_filename.empty()) {
            in_filename = argv[i];
        } else if (out_filename.empty()) {
            out_filename = argv[i];
        } else {
            rc = ERROR_ARGS;
        }
    }
    if (!is_rom && !is_disc) {
        printf("ERROR: must specify -r or -d\n");
        rc = ERROR_ARGS;
    }
    if (rc) {
        usage();
    } else {
        try {
            std::ofstream ofile(out_filename);
            if (!ofile.good()) {
                std::stringstream ss;
                ss << "Can't open " << out_filename;
                throw failure(ERROR_FILE, ss.str());
            }
            generate(in_filename, ofile);
        } catch (failure &e) {
            std::cerr << "ERROR: " << e.what() << "\n";
            rc = e.code();
        } catch (std::exception &e) {
            std::cerr << "ERROR: " << e.what() << "\n";
            rc = ERROR_UNKNOWN;
        }
        if (rc) {
            std::filesystem::remove(out_filename.c_str());
        }
    }
    return rc;
}

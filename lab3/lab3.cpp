#include <iostream>
#include <filesystem>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <openssl/evp.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <sys/stat.h> 

using namespace std;
namespace fs = filesystem;

// Функция обработки фатальных ошибок
void die(const string& msg, int code = 1) {
    cerr << "Ошибка: " << msg << "\n";
    exit(code);
}

// Функция для вычисления SHA-1 хэша файла
string get_sha1(const fs::path& filepath) {
    ifstream file(filepath, ios::binary);
    if (!file) {
        cerr << "Предупреждение: не удалось открыть " << filepath << "\n";
        return "";
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) die("не удалось создать контекст EVP");

    if (EVP_DigestInit_ex(ctx, EVP_sha1(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        die("не удалось инициализировать SHA-1");
    }

    char buffer[4096];
    while (file.read(buffer, sizeof(buffer))) {
        EVP_DigestUpdate(ctx, buffer, file.gcount());
    }
    if (file.gcount() > 0) {
        EVP_DigestUpdate(ctx, buffer, file.gcount());
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int lengthOfHash = 0;

    if (EVP_DigestFinal_ex(ctx, hash, &lengthOfHash) != 1) {
        EVP_MD_CTX_free(ctx);
        die("не удалось завершить хэширование");
    }

    EVP_MD_CTX_free(ctx);

    stringstream ss;
    for (unsigned int i = 0; i < lengthOfHash; ++i) {
        ss << hex << setw(2) << setfill('0') << (int)hash[i];
    }
    return ss.str();
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        die("Использование: " + string(argv[0]) + " <директория>");
    }

    fs::path root_dir = argv[1];

    if (!fs::is_directory(root_dir)) {
        die("директория " + root_dir.string() + " не существует");
    }

    unordered_map<string, fs::path> hash_map;
    int duplicates_found = 0;

    cout << "Сканирую: " << root_dir << "...\n";

    try {
        for (const auto& entry : fs::recursive_directory_iterator(root_dir)) {
            if (!fs::is_regular_file(entry.symlink_status())) continue;

            fs::path filepath = entry.path();
            string hash = get_sha1(filepath);

            if (hash.empty()) continue;

            if (hash_map.find(hash) != hash_map.end()) {
                fs::path original = hash_map[hash];
            
                struct stat stat_orig, stat_dup;
                if (stat(original.c_str(), &stat_orig) == 0 && 
                    lstat(filepath.c_str(), &stat_dup) == 0) {
                    
                    if (stat_orig.st_dev == stat_dup.st_dev && 
                        stat_orig.st_ino == stat_dup.st_ino) {
                        continue;
                    }
                    if (stat_orig.st_dev != stat_dup.st_dev) {
                        cerr << "Пропуск (разные ФС): " << filepath << "\n";
                        continue;
                    }
                }

                cout << "Найден дубликат: " << filepath << " == " << original << "\n";

                fs::path tmp_path = filepath;
                tmp_path += ".link.tmp";

                if (link(original.c_str(), tmp_path.c_str()) == 0) {
                    if (rename(tmp_path.c_str(), filepath.c_str()) == 0) {
                        cout << "  -> Заменен на жесткую ссылку.\n";
                        duplicates_found++;
                    } else {
                        cerr << "  -> Ошибка rename: " << strerror(errno) << "\n";
                        unlink(tmp_path.c_str());
                    }
                } else {
                    cerr << "  -> Ошибка link: " << strerror(errno) << "\n";
                }
            } else {
                hash_map[hash] = filepath;
            }
        }
    } catch (const fs::filesystem_error& e) {
        cerr << "Ошибка обхода директории: " << e.what() << "\n";
    }

    cout << "\nГотово. Обработано дубликатов: " << duplicates_found << "\n";
    return 0;
}

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mycp.h"
#include "myfind.h"

static void create_dummy_file(const char *filename, const char *content) {
    FILE *f = fopen(filename, "w");
    assert(f != NULL);
    fputs(content, f);
    fclose(f);
}

static int compare_files(const char *file1, const char *file2) {
    FILE *f1 = fopen(file1, "rb");
    FILE *f2 = fopen(file2, "rb");
    if (!f1 || !f2) {
        if (f1) fclose(f1);
        if (f2) fclose(f2);
        return 0;
    }

    int match = 1;
    while (1) {
        int c1 = fgetc(f1);
        int c2 = fgetc(f2);
        if (c1 != c2) {
            match = 0;
            break;
        }
        if (c1 == EOF) {
            break;
        }
    }

    fclose(f1);
    fclose(f2);
    return match;
}

static void test_mycp(void) {
    printf("[RUN] Testing mycp...\n");
    const char *src = "test_src.txt";
    const char *dst = "test_dst.txt";
    const char *text = "First line of OS test.\nSecond line with some words.\nThird line!\n";

    create_dummy_file(src, text);

    // 1. Успешное копирование
    int res = mycp(src, dst);
    assert(res == 0);
    assert(compare_files(src, dst) == 1);

    // 2. Обработка ошибки несуществующего файла
    int fail_res = mycp("non_existent_file_xyz.txt", dst);
    assert(fail_res == -1);

    unlink(src);
    unlink(dst);
    printf("[OK] mycp passed successfully!\n\n");
}

static void test_myfind(void) {
    printf("[RUN] Testing myfind...\n");
    const char *test_file = "test_find.txt";
    const char *content = "operating systems\n"
                          "computer architecture\n"
                          "systems programming\n"
                          "discrete math\n";

    create_dummy_file(test_file, content);

    // Ищем слово "systems" (строки 1 и 3 -> 2 совпадения)
    printf("Matches for 'systems':\n");
    int count = myfind(test_file, "systems");
    assert(count == 2);

    // Ищем слово, которого нет
    printf("Matches for 'linux':\n");
    int not_found_count = myfind(test_file, "linux");
    assert(not_found_count == 0);

    // Ошибка при чтении несуществующего файла
    int err_count = myfind("non_existent.txt", "systems");
    assert(err_count == -1);

    unlink(test_file);
    printf("[OK] myfind passed successfully!\n\n");
}

int main(void) {
    printf("================ STARTING TESTS ================\n\n");
    test_mycp();
    test_myfind();
    printf("================ ALL TESTS PASSED! ================\n");
    return 0;
}

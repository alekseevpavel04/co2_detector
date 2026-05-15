#include <unity.h>
#include "logic.h"

// Тесты чистой логики (запуск: pio test -e native).
// На Этапе 1 — только заглушка проверяет, что test-инфраструктура
// собирается. Реальные тесты появятся с Этапа 6.

void setUp(void) {}
void tearDown(void) {}

void test_logic_skeleton(void) {
    TEST_ASSERT_EQUAL_INT(1, logic_stage_check());
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_logic_skeleton);
    return UNITY_END();
}

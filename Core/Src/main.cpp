#include "main.h"
#include "cpp_main.h"
#include "ami_controller.h"

void cpp_main(void) {
    static AMIController controller;
    controller.Init();
    controller.Run();
}

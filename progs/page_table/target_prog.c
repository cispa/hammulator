
#include <unistd.h>

int main() {
  static const char msg[] = "Hello world -- in innocent program\n";
  if (write(1, msg, sizeof(msg) - 1)) {}
  return 44;
}

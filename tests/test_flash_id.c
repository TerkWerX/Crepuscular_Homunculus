#include <assert.h>
#include "../firmware/hal/fm1_flash_id.h"
int main(void) {
  assert(fl_jedec_ok(0x856014u));
  assert(fl_jedec_ok(0xB36014u));
  assert(!fl_jedec_ok(0));
  assert(!fl_jedec_ok(0x856015u));
  assert(!fl_jedec_ok(0xB36015u));
  assert(!fl_jedec_ok(0xffffffffu));
  return 0;
}

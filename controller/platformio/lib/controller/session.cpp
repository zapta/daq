
#include "session.h"

#include "rng.h"

namespace session {

static uint32_t session_id = 0;

void setup() {
  if (session_id != 0) {
    logger.error("Session already setup.");
    return;
  }
  auto status = HAL_RNG_GenerateRandomNumber(&hrng, &session_id);
  if (status != HAL_OK || session_id == 0) {
    error_handler::Panic();
  }
  logger.info("Random session id: [%08lx]", session_id);
}

uint32_t id() {
  // Make sure session is initialized.
  if (session_id == 0) {
    error_handler::Panic();
  }
  return session_id;
}

}  // namespace session

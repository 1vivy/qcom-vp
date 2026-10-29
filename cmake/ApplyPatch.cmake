find_program(GIT git REQUIRED)
execute_process(COMMAND "${GIT}" apply --reverse --check "${PATCH}"
  WORKING_DIRECTORY "${SOURCE}" RESULT_VARIABLE already OUTPUT_QUIET ERROR_QUIET)
if(already EQUAL 0)
  return()
endif()
execute_process(COMMAND "${GIT}" apply --check "${PATCH}"
  WORKING_DIRECTORY "${SOURCE}" RESULT_VARIABLE checked)
if(NOT checked EQUAL 0)
  message(FATAL_ERROR "The dependency does not match the patch preimage: ${PATCH}")
endif()
execute_process(COMMAND "${GIT}" apply "${PATCH}"
  WORKING_DIRECTORY "${SOURCE}" COMMAND_ERROR_IS_FATAL ANY)

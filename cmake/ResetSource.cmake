# Restore a fetched dependency checkout to its pinned revision before the
# patch chain runs again. The chain applies stacked patches, so a reverse check
# of one patch cannot detect an already patched tree.
# The checkout is a build cache. The script refuses any other revision.
find_program(GIT git REQUIRED)
execute_process(COMMAND "${GIT}" rev-parse HEAD
  WORKING_DIRECTORY "${SOURCE}" OUTPUT_VARIABLE head
  OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(NOT head STREQUAL REVISION)
  message(FATAL_ERROR "The dependency checkout ${SOURCE} is at ${head}, not ${REVISION}")
endif()
execute_process(COMMAND "${GIT}" checkout -- .
  WORKING_DIRECTORY "${SOURCE}" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${GIT}" clean -fdq
  WORKING_DIRECTORY "${SOURCE}" COMMAND_ERROR_IS_FATAL ANY)

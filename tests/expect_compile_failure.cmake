# Собирает цель, которая НЕ должна компилироваться, и проверяет:
#   1) сборка завершилась ошибкой;
#   2) в выводе компилятора упомянут нужный концепт (только имя — без текста
#      диагностики, который различается у g++ и clang и зависит от локали).
# Параметры: -DBUILD_DIR=... -DTARGET=... -DCONCEPT=... [-DCONFIG=...]
execute_process(
  COMMAND ${CMAKE_COMMAND} --build ${BUILD_DIR} --target ${TARGET} --config ${CONFIG}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE output)
if(result EQUAL 0)
  message(FATAL_ERROR "${TARGET} скомпилировался, хотя должен был нарушить концепт ${CONCEPT}")
endif()
string(FIND "${output}" "${CONCEPT}" position)
if(position EQUAL -1)
  message(FATAL_ERROR "${TARGET} не скомпилировался, но ошибка не связана с концептом ${CONCEPT}:\n${output}")
endif()
message(STATUS "${TARGET}: компилятор отверг код, концепт ${CONCEPT} упомянут")

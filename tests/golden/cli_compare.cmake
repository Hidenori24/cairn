# rmcli の出力を golden とバイト単位で比べる
#   -DRMCLI=… -DGOLDEN=… -DOUT=… -DEXPECT=… -DARGS="a;b;c"
execute_process(COMMAND ${RMCLI} ${ARGS} -o ${OUT} WORKING_DIRECTORY ${GOLDEN} RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "rmcli が失敗しました（${rc}）: ${ARGS}")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${OUT} ${GOLDEN}/${EXPECT} RESULT_VARIABLE diff)
if(NOT diff EQUAL 0)
  message(FATAL_ERROR "golden と一致しません: ${EXPECT}")
endif()

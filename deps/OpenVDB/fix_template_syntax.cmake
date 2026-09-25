# Rewrites OpT::template eval(...) -> OpT::eval(...) in OpenVDB's NodeManager.h (see OpenVDB.cmake). Idempotent.
file(READ "${NODE_MANAGER}" _content)
string(REPLACE "OpT::template eval" "OpT::eval" _content "${_content}")
file(WRITE "${NODE_MANAGER}" "${_content}")

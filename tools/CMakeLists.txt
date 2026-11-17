add_executable(mouse_sim mouse_sim.cpp)
target_link_libraries(mouse_sim PRIVATE mctl_net)
mctl_enable_warnings(mouse_sim)

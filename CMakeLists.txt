set(project_scratch_dir ${CMAKE_OUTPUT_DIRECTORY}/scratch/ns3-wifi6-aggregation-delay)

build_exec(
  EXECNAME wifi6-ampdu-latency
  EXECNAME_PREFIX scratch_ns3-wifi6-aggregation-delay_
  SOURCE_FILES wifi6-ampdu-latency.cc
  LIBRARIES_TO_LINK "${ns3-libs}" "${ns3-contrib-libs}"
  EXECUTABLE_DIRECTORY_PATH ${project_scratch_dir}
)

build_exec(
  EXECNAME wifi7-mlo-latency
  EXECNAME_PREFIX scratch_ns3-wifi6-aggregation-delay_
  SOURCE_FILES wifi7-mlo-latency.cc
  LIBRARIES_TO_LINK "${ns3-libs}" "${ns3-contrib-libs}"
  EXECUTABLE_DIRECTORY_PATH ${project_scratch_dir}
)

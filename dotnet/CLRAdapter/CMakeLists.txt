
add_library(DNP3CLRAdapter SHARED
src/CallbackAdapters.cpp
src/CallbackAdapters.h
src/ChangeSetAdapter.cpp
src/ChangeSetAdapter.h
src/ChannelAdapter.cpp
src/ChannelAdapter.h
src/ChannelListenerAdapter.h
src/CollectionAdapter.h
src/CommandSetBuilder.cpp
src/CommandSetBuilder.h
src/Conversions.cpp
src/Conversions.h
src/DNP3ManagerAdapter.cpp
src/DNP3ManagerAdapter.h
src/EventConverter.h
src/ICommandHeaderWorkAround.h
src/ListenCallbacksAdapter.cpp
src/ListenCallbacksAdapter.h
src/ListenerAdapter.h
src/Lock.h
src/LogAdapter.cpp
src/LogAdapter.h
src/MasterAdapter.cpp
src/MasterAdapter.h
src/MasterApplicationAdapter.h
src/MasterConversions.cpp
src/MasterConversions.h
src/MasterOperationsAdapter.cpp
src/MasterOperationsAdapter.h
src/MasterScanAdapter.h
src/MasterSessionAdapter.cpp
src/MasterSessionAdapter.h
src/OutstationAdapter.cpp
src/OutstationAdapter.h
src/OutstationApplicationAdapter.cpp
src/OutstationApplicationAdapter.h
src/OutstationCommandHandlerAdapter.cpp
src/OutstationCommandHandlerAdapter.h
src/SOEHandlerAdapter.cpp
src/SOEHandlerAdapter.h
src/SessionAcceptorAdapter.cpp
src/SessionAcceptorAdapter.h
src/StdIOWorkaround.cpp
src/StdIOWorkaround.h
src/TaskCallbackAdapter.h
)

set_target_properties(DNP3CLRAdapter PROPERTIES FOLDER "dotnet/libs" COMMON_LANGUAGE_RUNTIME "")
set_property(TARGET DNP3CLRAdapter PROPERTY VS_DOTNET_TARGET_FRAMEWORK_VERSION ${CLR_VERSION})
target_link_libraries(DNP3CLRAdapter DNP3CLRInterface asiodnp3)
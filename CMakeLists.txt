cmake_minimum_required(VERSION 3.21)

project(RealisticReflectionsMirrors VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(MIRRORS_OF_SKYRIM_BUILD_TESTS "Build the standalone mirror tests" OFF)

set(MIRRORS_OF_SKYRIM_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}")

set(SKSE_SUPPORT_XBYAK ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR ON CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory(
	"${MIRRORS_OF_SKYRIM_REPOSITORY_ROOT}/extern/CommonLibSSE-NG"
	"${CMAKE_CURRENT_BINARY_DIR}/CommonLibSSE"
	EXCLUDE_FROM_ALL)

find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(xbyak CONFIG REQUIRED)
find_package(directxmath CONFIG REQUIRED)

find_path(DETOURS_INCLUDE_DIRS "detours/detours.h" REQUIRED)
find_library(DETOURS_LIBRARY detours REQUIRED)

set(MIRRORS_OF_SKYRIM_SOURCES
	src/MirrorsOfSkyrimPlugin.cpp
	src/MirrorsOfSkyrimGeometryDrawObserver.cpp
	src/HandMirrorApprovedContentReadOnlyObserver.cpp
	src/HandMirrorBillboardStateLease.cpp
	src/HandMirrorParticleCameraLease.cpp
	src/HandMirrorPhysicalClipDepth.cpp
	src/HandMirrorReadOnlyObserver.cpp
	src/HandMirrorReflectionRuntime.cpp
	src/HandMirrorSettings.cpp
	src/HandMirrorSoftEffectBypass.cpp
	src/LeaseRegistry.cpp
	src/MirrorAuthorCatalogRuntimeLoader.cpp
	src/MirrorAuthorRegistrationRuntime.cpp
	src/MirrorAcquisition.cpp
	src/MirrorFixtureAnchors.cpp
	src/MirrorPlayerAimSteady.cpp
	src/MirrorSpellArtRetarget.cpp
	src/MirrorFleetRuntime.cpp
	src/MirrorOcclusionRuntime.cpp
	src/MirrorOcclusionQueries.cpp
	src/MirrorScenePruning.cpp
	src/MirrorPerformance.cpp
	src/MirrorCaptureProfile.cpp
	src/MirrorBenchmark.cpp
	src/MirrorPerformanceOverlay.cpp
	src/MirrorActivation.cpp
	src/MirrorCameraMath.cpp
	src/MirrorsOfSkyrimCameraOverride.cpp
	src/MirrorFramePublication.cpp
	src/MirrorMainSceneListBorrow.cpp
	src/MirrorsOfSkyrimPaneDelivery.cpp
	src/MirrorPaneNames.cpp
	src/MirrorPaneRenderer.cpp
	src/MirrorPaneSurface.cpp
	src/MirrorNifSurface.cpp
	src/MirrorSunShadowShader.cpp
	src/MirrorSunShadows.cpp
	src/MirrorShadowMaskPass.cpp
	src/MirrorENBParameters.cpp
	src/MirrorENBScreenMask.cpp
	src/MirrorSurfaceLights.cpp
	src/MirrorTextureSetSurface.cpp
	src/MirrorsOfSkyrimPlayerInclusion.cpp
	src/MirrorsOfSkyrimRecognition.cpp
	src/MirrorsOfSkyrimShadowMapBypass.cpp
	src/PeerDetection.cpp
	src/EngineDeviceIdentity.cpp
	src/PlanarMirrorMath.cpp
	src/MirrorPlayerDrawPassProbe.cpp
	src/SecondView.cpp
	src/SecondViewDepthEqualOverride.cpp
	src/SecondViewNoCull.cpp
	src/MirrorSecondViewRenderTarget.cpp
	src/StandingMirrorPlacement.cpp
	src/HandMirrorVRGripOffset.cpp
)
list(TRANSFORM MIRRORS_OF_SKYRIM_SOURCES
	PREPEND "${MIRRORS_OF_SKYRIM_REPOSITORY_ROOT}/")

add_library(RealisticReflectionsMirrors SHARED ${MIRRORS_OF_SKYRIM_SOURCES})
target_compile_features(RealisticReflectionsMirrors PRIVATE cxx_std_23)
target_compile_definitions(RealisticReflectionsMirrors PRIVATE
	SKYRIM
	SKSE_SUPPORT_XBYAK=1
	UNICODE
	_UNICODE
	NOMINMAX
	WIN32_LEAN_AND_MEAN
	MIRRORS_OF_SKYRIM_STANDALONE=1
	RR_VERSION_STR="${PROJECT_VERSION}"
)
target_precompile_headers(RealisticReflectionsMirrors PRIVATE
	"${MIRRORS_OF_SKYRIM_REPOSITORY_ROOT}/src/PCH.h")
target_include_directories(RealisticReflectionsMirrors
	PRIVATE
		"${MIRRORS_OF_SKYRIM_REPOSITORY_ROOT}/src"
		${DETOURS_INCLUDE_DIRS}
	PUBLIC
		"${MIRRORS_OF_SKYRIM_REPOSITORY_ROOT}/include"
)
target_link_libraries(RealisticReflectionsMirrors PRIVATE
	gdi32
	d3dcompiler
	CommonLibSSE::CommonLibSSE
	spdlog::spdlog
	nlohmann_json::nlohmann_json
	xbyak::xbyak
	Microsoft::DirectXMath
	${DETOURS_LIBRARY}
	d3d11
	bcrypt
)

if(MSVC)
	target_compile_options(RealisticReflectionsMirrors PRIVATE
		/MP1
		/W4
		/WX-
		/permissive-
		/Zc:__cplusplus
		/Zc:preprocessor
		/Zc:threadSafeInit
		/EHsc
		/wd4200
		/Gy
		/Gw
		"$<$<CONFIG:DEBUG>:/fp:strict;/ZI;/Od>"
		"$<$<CONFIG:RELEASE>:/Zi;/fp:precise;/O2;/Ob2;/Oi;/Ot>"
	)
	target_link_options(RealisticReflectionsMirrors PRIVATE
		"$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
		"$<$<CONFIG:RELEASE>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>"
	)
endif()

set_target_properties(RealisticReflectionsMirrors PROPERTIES
	OUTPUT_NAME "RealisticReflectionsMirrors"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin"
	RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/bin"
	RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_BINARY_DIR}/bin"
)

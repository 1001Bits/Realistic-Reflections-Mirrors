#ifndef REALISTIC_REFLECTIONS_MIRROR_AUTHORING_API_H
#define REALISTIC_REFLECTIONS_MIRROR_AUTHORING_API_H

/*
 * Public diagnostic C ABI for declaring third-party rectangular mirror bases.
 *
 * This header retains the frozen stage-1 layouts.  A separately marker-gated
 * stage-2 diagnostic may expose RR_GetMirrorAuthoringAPI only during one
 * synchronous PostLoad collection callback.  Declarations are copied as
 * provisional values; only a normal true broadcast freezes/logs them, while a
 * false or faulting broadcast aborts and exposes none.  When a second, unshipped
 * diagnostic marker is also latched, the exact SE/AE DataLoaded adapter resolves
 * the clean frozen values as read-only TESObjectSTAT identity evidence.  That
 * audit authorizes no schema-v2 mirror and never enters recognition, capture, or
 * delivery.
 *
 * The ABI deliberately exposes no C++, CommonLib, Skyrim, or allocator-owned
 * type.  register_base is synchronous: the diagnostic implementation copies
 * every accepted byte before it returns and never retains the input pointer.
 */

#include <stddef.h>
#include <stdint.h>

#if UINTPTR_MAX != UINT64_MAX
#	error RealisticReflections_MirrorAuthoringAPI requires a 64-bit consumer
#endif

#if defined(_WIN32)
#	define RR_MIRROR_AUTHOR_CALL __cdecl
#	if defined(RR_MIRROR_AUTHOR_API_BUILD)
#		define RR_MIRROR_AUTHOR_PUBLIC __declspec(dllexport)
#	else
#		define RR_MIRROR_AUTHOR_PUBLIC
#	endif
#else
#	define RR_MIRROR_AUTHOR_CALL
#	define RR_MIRROR_AUTHOR_PUBLIC
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define RR_MIRROR_AUTHOR_API_VERSION_1 UINT32_C(1)
#define RR_MIRROR_AUTHOR_BASE_VERSION_1 UINT32_C(1)
#define RR_MIRROR_AUTHOR_SCHEMA_RECTANGULAR_V2 UINT32_C(2)
#define RR_MIRROR_AUTHOR_MAX_PLUGIN_FILENAME_BYTES UINT32_C(255)
#define RR_MIRROR_AUTHOR_MAX_REGISTERED_BASES UINT32_C(256)

/*
 * Provisional stage-2 diagnostic collection message.
 *
 * The sender is the exact SKSE plugin name below.  The message type is the
 * deterministic little-endian FourCC "RRMA":
 *   'R' | ('R' << 8) | ('M' << 16) | ('A' << 24) == 0x414D5252.
 *
 * The message has no payload.  A listener may query version 1 and synchronously
 * call register_base only while handling this exact message.  The query returns
 * NULL before dispatch and after every listener returns.  This message protocol
 * is provisional even though the version-1 C layouts below remain frozen.
 */
#define RR_MIRROR_AUTHOR_PROVISIONAL_MESSAGE_COLLECT_V1 UINT32_C(0x414D5252)
#define RR_MIRROR_AUTHOR_PROVISIONAL_MESSAGE_SENDER "RealisticReflections"

/* Results returned by RR_MirrorAuthorRegisterBaseFn. */
#define RR_MIRROR_AUTHOR_RESULT_SUCCESS UINT32_C(0)
#define RR_MIRROR_AUTHOR_RESULT_IDEMPOTENT_DUPLICATE UINT32_C(1)
#define RR_MIRROR_AUTHOR_RESULT_INVALID_ARGUMENT UINT32_C(2)
#define RR_MIRROR_AUTHOR_RESULT_INCOMPATIBLE_STRUCT UINT32_C(3)
#define RR_MIRROR_AUTHOR_RESULT_COLLECTION_CLOSED UINT32_C(4)
#define RR_MIRROR_AUTHOR_RESULT_INVALID_PLUGIN_NAME UINT32_C(5)
#define RR_MIRROR_AUTHOR_RESULT_INVALID_LOCAL_FORM_ID UINT32_C(6)
#define RR_MIRROR_AUTHOR_RESULT_UNSUPPORTED_SCHEMA UINT32_C(7)
#define RR_MIRROR_AUTHOR_RESULT_UNSUPPORTED_FLAGS UINT32_C(8)
#define RR_MIRROR_AUTHOR_RESULT_FIRST_PARTY_NAMESPACE_RESERVED UINT32_C(9)
#define RR_MIRROR_AUTHOR_RESULT_CONFLICTING_DECLARATION UINT32_C(10)
#define RR_MIRROR_AUTHOR_RESULT_CAPACITY_EXCEEDED UINT32_C(11)
#define RR_MIRROR_AUTHOR_RESULT_CATALOG_QUARANTINED UINT32_C(12)
#define RR_MIRROR_AUTHOR_RESULT_LIFECYCLE_MISMATCH UINT32_C(13)

/*
 * One exact source-plugin/local-FormID declaration.
 *
 * Zero-initialize the complete structure, then set struct_size,
 * struct_version, source_plugin_length, local_form_id, mirror_schema, and the
 * source_plugin bytes. source_plugin is a TES plugin basename, never a path.
 * It must contain source_plugin_length printable ASCII bytes followed by one
 * NUL; every remaining byte and every reserved field must remain zero.
 * local_form_id is load-order independent and must not contain runtime load
 * index bits. An explicit .esl basename is restricted to its owned compact
 * range 0x800..0xFFF; a light-flagged .esp is checked by the separately
 * marker-gated exact TES resolution diagnostic. Version 1 supports schema 2 and
 * flags == 0 only.
 */
typedef struct RR_MirrorAuthorBaseV1
{
	uint32_t struct_size;
	uint32_t struct_version;
	uint32_t mirror_schema;
	uint32_t flags;

	uint32_t local_form_id;
	uint32_t source_plugin_length;
	char source_plugin[256];

	uint32_t reserved_u32[2];
	uint64_t reserved[4];
} RR_MirrorAuthorBaseV1;

typedef uint32_t(RR_MIRROR_AUTHOR_CALL* RR_MirrorAuthorRegisterBaseFn)(
	uint64_t collection_token,
	const RR_MirrorAuthorBaseV1* declaration);

/*
 * Version-1 declaration table. max_registered_bases is fixed at 256 for this
 * ABI version. collection_token is a nonzero immutable value for this exact
 * table/window and must be passed back on every register_base call. A retained
 * function pointer plus a stale token cannot mutate a later collection. A
 * diagnostic producer returns this table only during its documented
 * pre-DataLoaded collection window; after freeze or abort, register_base
 * returns RR_MIRROR_AUTHOR_RESULT_COLLECTION_CLOSED.
 */
typedef struct RR_MirrorAuthoringAPI
{
	uint32_t struct_size;
	uint32_t api_version;
	uint32_t declaration_version;
	uint32_t max_registered_bases;
	uint64_t collection_token;
	RR_MirrorAuthorRegisterBaseFn register_base;
	uint64_t reserved[4];
} RR_MirrorAuthoringAPI;

/*
 * Diagnostic query export.  It returns NULL unless version 1 is requested and
 * the marker-latched synchronous PostLoad collection dispatch is active on its
 * exact owner thread.  A non-NULL table grants collection access only; it never
 * grants mirror recognition or renderer authority.
 */
RR_MIRROR_AUTHOR_PUBLIC const RR_MirrorAuthoringAPI* RR_MIRROR_AUTHOR_CALL
RR_GetMirrorAuthoringAPI(uint32_t requested_api_version);

#ifdef __cplusplus
} /* extern "C" */

static_assert(sizeof(RR_MirrorAuthorBaseV1) == 320,
	"RR_MirrorAuthorBaseV1 ABI drift");
static_assert(offsetof(RR_MirrorAuthorBaseV1, source_plugin) == 24,
	"RR_MirrorAuthorBaseV1 layout drift");
static_assert(offsetof(RR_MirrorAuthorBaseV1, reserved) == 288,
	"RR_MirrorAuthorBaseV1 layout drift");
static_assert(sizeof(RR_MirrorAuthoringAPI) == 64,
	"RR_MirrorAuthoringAPI ABI drift");
static_assert(offsetof(RR_MirrorAuthoringAPI, collection_token) == 16,
	"RR_MirrorAuthoringAPI layout drift");
static_assert(offsetof(RR_MirrorAuthoringAPI, register_base) == 24,
	"RR_MirrorAuthoringAPI layout drift");
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(RR_MirrorAuthorBaseV1) == 320,
	"RR_MirrorAuthorBaseV1 ABI drift");
_Static_assert(offsetof(RR_MirrorAuthorBaseV1, source_plugin) == 24,
	"RR_MirrorAuthorBaseV1 layout drift");
_Static_assert(offsetof(RR_MirrorAuthorBaseV1, reserved) == 288,
	"RR_MirrorAuthorBaseV1 layout drift");
_Static_assert(sizeof(RR_MirrorAuthoringAPI) == 64,
	"RR_MirrorAuthoringAPI ABI drift");
_Static_assert(offsetof(RR_MirrorAuthoringAPI, collection_token) == 16,
	"RR_MirrorAuthoringAPI layout drift");
_Static_assert(offsetof(RR_MirrorAuthoringAPI, register_base) == 24,
	"RR_MirrorAuthoringAPI layout drift");
#endif

#endif /* REALISTIC_REFLECTIONS_MIRROR_AUTHORING_API_H */

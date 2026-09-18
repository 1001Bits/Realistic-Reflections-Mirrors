#include "MirrorPaneRenderer.h"
#include "EngineDeviceIdentity.h"
#include "MirrorNifContract.h"
#include "PlanarMipChainPolicy.h"
#include "HandMirrorLoweredPresentationPolicy.h"

#include "D3D11OutputMergerState.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>

namespace MirrorPaneRenderer
{
	namespace
	{
#include "MirrorPaneVertexShader.inc"
#include "MirrorPanePixelShader.inc"

		constexpr float kProjectionEpsilon = 1.0e-4f;
		constexpr float kMinimumProjectedArea = 1.0e-6f;
		constexpr float kMinimumExtentSquared = 1.0e-6f;
		constexpr std::uint32_t kMaximumTextureDimension = 16384;
		constexpr std::size_t kOwnedRendererResourceCount = 16;
#if defined(RR_MIRROR_PANE_RENDERER_TEST_HOOKS)
		static_assert(
			kOwnedRendererResourceCount == TestHooks::kRendererResourceCount);
		struct CleanupFaultInjectionState
		{
			TestHooks::CleanupFaultSite site{ TestHooks::CleanupFaultSite::kNone };
			TestHooks::CleanupTelemetry telemetry{};
			bool consumed{ false };
		};
		thread_local CleanupFaultInjectionState g_cleanupFaultInjection{};
#endif
		constexpr GUID kD3DDebugObjectName{
			0x429b8c22, 0x9188, 0x4b0c, { 0x87, 0x42, 0xac, 0xb0, 0xbf, 0x85, 0xc2, 0x00 }
		};

		struct Vertex
		{
			DirectX::XMFLOAT2 unitPosition;
		};

		constexpr std::array<Vertex, 4> kVertices{
			Vertex{ { -1.0f, -1.0f } },
			Vertex{ { +1.0f, -1.0f } },
			Vertex{ { +1.0f, +1.0f } },
			Vertex{ { -1.0f, +1.0f } }
		};
		constexpr std::array<std::uint16_t, 6> kIndices{ 0, 1, 2, 0, 2, 3 };

		// These exact authored boundary vertices remain the CPU coverage authority.
		// Presentation itself uses one continuous covering triangle and clips to the
		// selected boundary in MirrorPane.hlsl.  This preserves the no-DSV aperture
		// contract without introducing 16/24 primitive-local sampling sectors.
		constexpr float kHandApertureExtent = 7.0f;
		[[nodiscard]] constexpr float HandPaneUnit(float a_authored) noexcept
		{
			return a_authored / kHandApertureExtent;
		}
		constexpr std::array<Vertex, 17> kHandRound16Vertices{
			Vertex{ { 0.0f, 0.0f } },
			Vertex{ { HandPaneUnit(0.0f), HandPaneUnit(-7.0f) } },
			Vertex{ { HandPaneUnit(3.0f), HandPaneUnit(-6.0f) } },
			Vertex{ { HandPaneUnit(5.0f), HandPaneUnit(-5.0f) } },
			Vertex{ { HandPaneUnit(6.0f), HandPaneUnit(-3.0f) } },
			Vertex{ { HandPaneUnit(7.0f), HandPaneUnit(0.0f) } },
			Vertex{ { HandPaneUnit(6.0f), HandPaneUnit(3.0f) } },
			Vertex{ { HandPaneUnit(5.0f), HandPaneUnit(5.0f) } },
			Vertex{ { HandPaneUnit(3.0f), HandPaneUnit(6.0f) } },
			Vertex{ { HandPaneUnit(0.0f), HandPaneUnit(7.0f) } },
			Vertex{ { HandPaneUnit(-3.0f), HandPaneUnit(6.0f) } },
			Vertex{ { HandPaneUnit(-5.0f), HandPaneUnit(5.0f) } },
			Vertex{ { HandPaneUnit(-6.0f), HandPaneUnit(3.0f) } },
			Vertex{ { HandPaneUnit(-7.0f), HandPaneUnit(0.0f) } },
			Vertex{ { HandPaneUnit(-6.0f), HandPaneUnit(-3.0f) } },
			Vertex{ { HandPaneUnit(-5.0f), HandPaneUnit(-5.0f) } },
			Vertex{ { HandPaneUnit(-3.0f), HandPaneUnit(-6.0f) } }
		};
		constexpr std::array<Vertex, 25> kHandFiligree24Vertices{
			Vertex{ { 0.0f, 0.0f } },
			Vertex{ { HandPaneUnit(0.0f), HandPaneUnit(-7.0f) } },
			Vertex{ { HandPaneUnit(2.0f), HandPaneUnit(-7.0f) } },
			Vertex{ { HandPaneUnit(4.0f), HandPaneUnit(-6.0f) } },
			Vertex{ { HandPaneUnit(5.0f), HandPaneUnit(-5.0f) } },
			Vertex{ { HandPaneUnit(6.0f), HandPaneUnit(-4.0f) } },
			Vertex{ { HandPaneUnit(7.0f), HandPaneUnit(-2.0f) } },
			Vertex{ { HandPaneUnit(7.0f), HandPaneUnit(0.0f) } },
			Vertex{ { HandPaneUnit(7.0f), HandPaneUnit(2.0f) } },
			Vertex{ { HandPaneUnit(6.0f), HandPaneUnit(4.0f) } },
			Vertex{ { HandPaneUnit(5.0f), HandPaneUnit(5.0f) } },
			Vertex{ { HandPaneUnit(4.0f), HandPaneUnit(6.0f) } },
			Vertex{ { HandPaneUnit(2.0f), HandPaneUnit(7.0f) } },
			Vertex{ { HandPaneUnit(0.0f), HandPaneUnit(7.0f) } },
			Vertex{ { HandPaneUnit(-2.0f), HandPaneUnit(7.0f) } },
			Vertex{ { HandPaneUnit(-4.0f), HandPaneUnit(6.0f) } },
			Vertex{ { HandPaneUnit(-5.0f), HandPaneUnit(5.0f) } },
			Vertex{ { HandPaneUnit(-6.0f), HandPaneUnit(4.0f) } },
			Vertex{ { HandPaneUnit(-7.0f), HandPaneUnit(2.0f) } },
			Vertex{ { HandPaneUnit(-7.0f), HandPaneUnit(0.0f) } },
			Vertex{ { HandPaneUnit(-7.0f), HandPaneUnit(-2.0f) } },
			Vertex{ { HandPaneUnit(-6.0f), HandPaneUnit(-4.0f) } },
			Vertex{ { HandPaneUnit(-5.0f), HandPaneUnit(-5.0f) } },
			Vertex{ { HandPaneUnit(-4.0f), HandPaneUnit(-6.0f) } },
			Vertex{ { HandPaneUnit(-2.0f), HandPaneUnit(-7.0f) } }
		};

		// A tight equilateral cover remains one continuous primitive, but unlike the
		// historical (-1,-1)/(+3,-1)/(-1,+3) cover it has no directionally biased
		// far vertex.  Its 1.05-unit incircle contains every exact authored boundary
		// point (the furthest filigree point is sqrt(53)/7 ~= 1.040), while reducing
		// projective-W excursions at close/grazing hand-mirror angles.
		constexpr float kHandCoverInradius = 1.05f;
		constexpr float kHandCoverHalfWidth = 1.81865335f;  // sqrt(3) * 1.05
		constexpr std::array<Vertex, 3> kHandCoverVertices{
			Vertex{ { -kHandCoverHalfWidth, -kHandCoverInradius } },
			Vertex{ { +kHandCoverHalfWidth, -kHandCoverInradius } },
			Vertex{ { 0.0f, 2.0f * kHandCoverInradius } }
		};
		constexpr std::array<std::uint16_t, 3> kHandCoverIndices{ 0, 1, 2 };
		[[nodiscard]] constexpr float HandCoverEdgeValue(
			const Vertex& a_start,
			const Vertex& a_end,
			const Vertex& a_point) noexcept
		{
			return (a_end.unitPosition.x - a_start.unitPosition.x) *
				(a_point.unitPosition.y - a_start.unitPosition.y) -
				(a_end.unitPosition.y - a_start.unitPosition.y) *
				(a_point.unitPosition.x - a_start.unitPosition.x);
		}

		[[nodiscard]] constexpr bool HandCoverContains(
			const Vertex& a_point) noexcept
		{
			return HandCoverEdgeValue(
				kHandCoverVertices[0], kHandCoverVertices[1], a_point) >= 0.0f &&
			       HandCoverEdgeValue(
				   kHandCoverVertices[1], kHandCoverVertices[2], a_point) >= 0.0f &&
			       HandCoverEdgeValue(
				   kHandCoverVertices[2], kHandCoverVertices[0], a_point) >= 0.0f;
		}

		template <std::size_t Size>
		[[nodiscard]] constexpr bool HandCoverContainsAperture(
			const std::array<Vertex, Size>& a_vertices) noexcept
		{
			for (const auto& vertex : a_vertices) {
				if (!HandCoverContains(vertex))
					return false;
			}
			return true;
		}
		static_assert(kHandRound16Vertices.size() == 17);
		static_assert(kHandFiligree24Vertices.size() == 25);
		static_assert(kHandCoverVertices.size() == 3);
		static_assert(kHandCoverIndices.size() == 3);
		static_assert(
			kHandCoverVertices[0].unitPosition.x ==
				-kHandCoverVertices[1].unitPosition.x &&
			kHandCoverVertices[0].unitPosition.y ==
				kHandCoverVertices[1].unitPosition.y &&
			kHandCoverVertices[2].unitPosition.x == 0.0f);
		static_assert(HandCoverContainsAperture(kHandRound16Vertices));
		static_assert(HandCoverContainsAperture(kHandFiligree24Vertices));

		constexpr std::uint64_t kFNVOffset = UINT64_C(14695981039346656037);
		constexpr std::uint64_t kFNVPrime = UINT64_C(1099511628211);
		[[nodiscard]] constexpr std::uint64_t HashText(
			const std::string_view a_text) noexcept
		{
			std::uint64_t value = kFNVOffset;
			for (const auto character : a_text) {
				value ^= static_cast<std::uint8_t>(character);
				value *= kFNVPrime;
			}
			return value == 0 ? 1 : value;
		}

		constexpr std::uint64_t kFiligreeStyleIdentity =
			HashText("gilded-noble-round-filigree-v1");
		constexpr std::uint64_t kFiligreeApertureContractIdentity =
			HashText("filigree-v1:frame-opening-24gon-aperture:7,7,1,2");

		enum class PaneApertureKind : std::uint8_t
		{
			kWallQuad,
			kHandRound16,
			kHandFiligree24
		};

		struct alignas(16) PaneConstants
		{
			DirectX::XMFLOAT4X4 mainViewProjection;
			DirectX::XMFLOAT4X4 reflectedViewProjection;
			DirectX::XMFLOAT4 mainOrigin;
			DirectX::XMFLOAT4 reflectedOrigin;
			DirectX::XMFLOAT4 paneCenter;
			DirectX::XMFLOAT4 paneTangentExtent;
			DirectX::XMFLOAT4 paneBitangentExtent;
			DirectX::XMFLOAT4 uvClamp;
			DirectX::XMFLOAT4X4 inverseReflectedViewProjection;
			DirectX::XMFLOAT4X4 previousMainViewProjection;
			DirectX::XMFLOAT4X4 previousReflectedViewProjection;
			DirectX::XMFLOAT4X4 previousInverseReflectedViewProjection;
			DirectX::XMFLOAT4 previousMainOrigin;
			DirectX::XMFLOAT4 previousReflectedOrigin;
			DirectX::XMFLOAT4 previousPaneCenter;
			DirectX::XMFLOAT4 previousPaneTangentExtent;
			DirectX::XMFLOAT4 previousPaneBitangentExtent;
			DirectX::XMFLOAT4 previousPaneNormal;
			DirectX::XMFLOAT4 motionViewport;
		};
		static_assert(sizeof(PaneConstants) == 37 * 16);
		static_assert(sizeof(PaneConstants) % 16 == 0);

		enum class ShaderMotionMode : std::uint8_t
		{
			kDisabled = 0,
			kDepthReconstructedWorld = 1,
			kPaneAffine = 2,
			kRejectHistory = 3
		};

		enum class ProjectionCoverage : std::uint8_t
		{
			kRejected,
			kMainViewNotVisible,
			kReflectedUncovered,
			kPartial,
			kFull
		};

		void SetDebugName(ID3D11DeviceChild* a_object, const char* a_name) noexcept
		{
			if (a_object && a_name) {
				(void) a_object->SetPrivateData(
					kD3DDebugObjectName,
					static_cast<UINT>(std::strlen(a_name)),
					a_name);
			}
		}

		[[nodiscard]] bool Finite(float a_value) noexcept
		{
			return std::isfinite(a_value);
		}

		[[nodiscard]] bool Finite(const DirectX::XMFLOAT3& a_value) noexcept
		{
			return Finite(a_value.x) && Finite(a_value.y) && Finite(a_value.z);
		}

		[[nodiscard]] bool Finite(const DirectX::XMFLOAT4& a_value) noexcept
		{
			return Finite(a_value.x) && Finite(a_value.y) && Finite(a_value.z) &&
			       Finite(a_value.w);
		}

		[[nodiscard]] bool Finite(const DirectX::XMFLOAT4X4& a_matrix) noexcept
		{
			const float* values = &a_matrix._11;
			for (std::size_t index = 0; index < 16; ++index) {
				if (!Finite(values[index]))
					return false;
			}
			return true;
		}

		[[nodiscard]] float LengthSquared(const DirectX::XMFLOAT3& a_value) noexcept
		{
			return a_value.x * a_value.x + a_value.y * a_value.y +
			       a_value.z * a_value.z;
		}

		[[nodiscard]] bool ValidPane(const PaneTransform& a_pane) noexcept
		{
			if (!Finite(a_pane.center) || !Finite(a_pane.tangentExtent) ||
				!Finite(a_pane.bitangentExtent) ||
				!HandMirrorRuntimeBridgePolicy::IsValidOwner(a_pane.owner))
				return false;

			const float tangentLengthSquared = LengthSquared(a_pane.tangentExtent);
			const float bitangentLengthSquared = LengthSquared(a_pane.bitangentExtent);
			if (!Finite(tangentLengthSquared) || !Finite(bitangentLengthSquared) ||
				tangentLengthSquared <= kMinimumExtentSquared ||
				bitangentLengthSquared <= kMinimumExtentSquared)
				return false;

			const DirectX::XMFLOAT3 cross{
				a_pane.tangentExtent.y * a_pane.bitangentExtent.z -
					a_pane.tangentExtent.z * a_pane.bitangentExtent.y,
				a_pane.tangentExtent.z * a_pane.bitangentExtent.x -
					a_pane.tangentExtent.x * a_pane.bitangentExtent.z,
				a_pane.tangentExtent.x * a_pane.bitangentExtent.y -
					a_pane.tangentExtent.y * a_pane.bitangentExtent.x
			};
			const float crossLengthSquared = LengthSquared(cross);
			const float relativeThreshold =
				kMinimumExtentSquared * tangentLengthSquared * bitangentLengthSquared;
			return Finite(crossLengthSquared) && crossLengthSquared > relativeThreshold;
		}

		[[nodiscard]] bool ValidImmediateMotionHistory(
			const DrawRequest& a_request) noexcept
		{
			const auto& history = a_request.motionHistory;
			return history.valid && history.captureSequence != 0 &&
			       history.deliveryFrame != 0 && a_request.deliveryFrame != 0 &&
			       history.deliveryFrame == a_request.deliveryFrame - 1 &&
			       history.captureSequence <= a_request.frame.captureSequence &&
			       HandMirrorRuntimeBridgePolicy::SameOwner(
				   history.pane.owner, a_request.pane.owner) &&
			       ValidPane(history.pane) &&
			       Finite(history.mainViewProjection) && Finite(history.mainOrigin);
		}

		[[nodiscard]] bool TryInvertFinite(
			const DirectX::XMFLOAT4X4& a_matrix,
			DirectX::XMFLOAT4X4& a_inverse) noexcept
		{
			a_inverse = {};
			if (!Finite(a_matrix))
				return false;
			DirectX::XMVECTOR determinant{};
			const auto inverse = DirectX::XMMatrixInverse(
				&determinant, DirectX::XMLoadFloat4x4(&a_matrix));
			const float determinantValue = DirectX::XMVectorGetX(determinant);
			if (!Finite(determinantValue) ||
				std::abs(determinantValue) <= std::numeric_limits<float>::min()) {
				return false;
			}
			DirectX::XMStoreFloat4x4(&a_inverse, inverse);
			return Finite(a_inverse);
		}

		void ReleaseQueryInterfaceSEH(
			IUnknown*& value,
			bool& faulted) noexcept
		{
			auto* pending = value;
			value = nullptr;
			if (!pending)
				return;
#if defined(RR_MIRROR_PANE_RENDERER_TEST_HOOKS)
			++g_cleanupFaultInjection.telemetry.queryReleasesAttempted;
#endif
			__try {
				pending->Release();
#if defined(RR_MIRROR_PANE_RENDERER_TEST_HOOKS)
				++g_cleanupFaultInjection.telemetry.queryReleasesCompleted;
#endif
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				faulted = true;
			}
		}

		template <class T>
		void ReleaseQueryInterfaceSEH(T*& value, bool& faulted) noexcept
		{
			IUnknown* unknown = value;
			value = nullptr;
			ReleaseQueryInterfaceSEH(unknown, faulted);
		}

		template <class T>
		[[nodiscard]] bool ScrubQueryComPtrSEH(
			Microsoft::WRL::ComPtr<T>& value) noexcept
		{
			auto* raw = value.Detach();
			bool faulted = false;
			ReleaseQueryInterfaceSEH(raw, faulted);
			return !faulted;
		}

		template <class T>
		struct GuardedQueryOwner
		{
			Microsoft::WRL::ComPtr<T> value{};

			~GuardedQueryOwner()
			{
				(void)ScrubQueryComPtrSEH(value);
			}

			GuardedQueryOwner() = default;
			GuardedQueryOwner(const GuardedQueryOwner&) = delete;
			GuardedQueryOwner& operator=(const GuardedQueryOwner&) = delete;
		};

		struct DrawQueryResources
		{
			Microsoft::WRL::ComPtr<ID3D11Device> contextDevice{};
			Microsoft::WRL::ComPtr<ID3D11Resource> reflectionResource{};
			Microsoft::WRL::ComPtr<ID3D11Resource> reflectionDepthResource{};
			Microsoft::WRL::ComPtr<ID3D11Resource> colorTargetResource{};
			Microsoft::WRL::ComPtr<ID3D11Resource> depthTargetResource{};
			Microsoft::WRL::ComPtr<ID3D11Resource> motionTargetResource{};

			~DrawQueryResources()
			{
				(void)Scrub();
			}

			[[nodiscard]] bool Scrub() noexcept
			{
				// Detach every owner first, then guard each Release independently.
				auto* context = contextDevice.Detach();
				auto* reflection = reflectionResource.Detach();
				auto* reflectionDepth = reflectionDepthResource.Detach();
				auto* color = colorTargetResource.Detach();
				auto* depth = depthTargetResource.Detach();
				auto* motion = motionTargetResource.Detach();
				bool faulted = false;
				ReleaseQueryInterfaceSEH(motion, faulted);
				ReleaseQueryInterfaceSEH(depth, faulted);
				ReleaseQueryInterfaceSEH(color, faulted);
				ReleaseQueryInterfaceSEH(reflectionDepth, faulted);
				ReleaseQueryInterfaceSEH(reflection, faulted);
				ReleaseQueryInterfaceSEH(context, faulted);
				return !faulted;
			}
		};

		[[nodiscard]] bool GetChildDeviceSEH(
			ID3D11DeviceChild* child,
			ID3D11Device*& output,
			bool& nativeFaulted) noexcept
		{
			output = nullptr;
			if (!child)
				return false;
			__try {
				child->GetDevice(&output);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				nativeFaulted = true;
			}
			return output != nullptr && !nativeFaulted;
		}

		[[nodiscard]] bool GetContextDeviceSEH(
			ID3D11DeviceContext* context,
			ID3D11Device*& output,
			bool& nativeFaulted) noexcept
		{
			output = nullptr;
			if (!context)
				return false;
			__try {
				context->GetDevice(&output);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				nativeFaulted = true;
			}
			return output != nullptr && !nativeFaulted;
		}

		[[nodiscard]] bool BelongsToDevice(
			ID3D11DeviceChild* a_child,
			ID3D11Device* a_expected,
			bool& nativeFaulted) noexcept
		{
			if (!a_child || !a_expected)
				return false;
			ID3D11Device* rawDevice = nullptr;
			const bool queried = GetChildDeviceSEH(
				a_child, rawDevice, nativeFaulted);
			// Under a d3d11 wrapper (ENB) every D3D child reports the real device
			// while the renderer holds the forwarding one; both are one ownership.
			const bool same = queried &&
				EngineDeviceIdentityPolicy::OwnedByEngineDevice(
					EngineDeviceIdentity::Current(
						reinterpret_cast<std::uintptr_t>(a_expected)),
					reinterpret_cast<std::uintptr_t>(rawDevice));
			bool releaseFaulted = false;
			ReleaseQueryInterfaceSEH(rawDevice, releaseFaulted);
			nativeFaulted = nativeFaulted || releaseFaulted;
			return same && !nativeFaulted;
		}

		[[nodiscard]] bool GetTexture2D(
			ID3D11View* a_view,
			Microsoft::WRL::ComPtr<ID3D11Resource>& a_resource,
			Microsoft::WRL::ComPtr<ID3D11Texture2D>& a_texture,
			bool& nativeFaulted) noexcept
		{
			if (!a_view || a_resource || a_texture)
				return false;
			ID3D11Resource* rawResource = nullptr;
			ID3D11Texture2D* rawTexture = nullptr;
			HRESULT queryResult = E_FAIL;
			__try {
				a_view->GetResource(&rawResource);
#if defined(RR_MIRROR_PANE_RENDERER_TEST_HOOKS)
				if (!g_cleanupFaultInjection.consumed && rawResource &&
					g_cleanupFaultInjection.site == TestHooks::CleanupFaultSite::
						kPublishedFrameAfterResourceQuery) {
					g_cleanupFaultInjection.consumed = true;
					RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
				}
#endif
				if (rawResource) {
					queryResult = rawResource->QueryInterface(
						__uuidof(ID3D11Texture2D),
						reinterpret_cast<void**>(&rawTexture));
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				nativeFaulted = true;
			}
			a_resource.Attach(rawResource);
			a_texture.Attach(rawTexture);
			return !nativeFaulted && a_resource && a_texture &&
				SUCCEEDED(queryResult);
		}

		[[nodiscard]] bool GetTextureDescriptionSEH(
			ID3D11Texture2D* texture,
			D3D11_TEXTURE2D_DESC& output,
			bool& nativeFaulted) noexcept
		{
			output = {};
			if (!texture)
				return false;
			__try {
				texture->GetDesc(&output);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				nativeFaulted = true;
			}
			return !nativeFaulted;
		}

		[[nodiscard]] bool GetSRVDescriptionSEH(
			ID3D11ShaderResourceView* view,
			D3D11_SHADER_RESOURCE_VIEW_DESC& output,
			bool& nativeFaulted) noexcept
		{
			output = {};
			if (!view)
				return false;
			__try {
				view->GetDesc(&output);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				nativeFaulted = true;
			}
			return !nativeFaulted;
		}

		[[nodiscard]] bool GetDSVDescriptionSEH(
			ID3D11DepthStencilView* view,
			D3D11_DEPTH_STENCIL_VIEW_DESC& output,
			bool& nativeFaulted) noexcept
		{
			output = {};
			if (!view)
				return false;
			__try {
				view->GetDesc(&output);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				nativeFaulted = true;
			}
			return !nativeFaulted;
		}

		[[nodiscard]] bool GetRTVDescriptionSEH(
			ID3D11RenderTargetView* view,
			D3D11_RENDER_TARGET_VIEW_DESC& output,
			bool& nativeFaulted) noexcept
		{
			output = {};
			if (!view)
				return false;
			__try {
				view->GetDesc(&output);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				nativeFaulted = true;
			}
			return !nativeFaulted;
		}

		[[nodiscard]] bool QueryTexturePairSEH(
			ID3D11Resource* colorResource,
			ID3D11Resource* depthResource,
			ID3D11Texture2D*& colorTexture,
			ID3D11Texture2D*& depthTexture,
			bool& nativeFaulted) noexcept
		{
			colorTexture = nullptr;
			depthTexture = nullptr;
			HRESULT colorQuery = E_FAIL;
			HRESULT depthQuery = E_FAIL;
			if (!colorResource || !depthResource)
				return false;
			__try {
				colorQuery = colorResource->QueryInterface(
					__uuidof(ID3D11Texture2D),
					reinterpret_cast<void**>(&colorTexture));
				depthQuery = depthResource->QueryInterface(
					__uuidof(ID3D11Texture2D),
					reinterpret_cast<void**>(&depthTexture));
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				nativeFaulted = true;
			}
			return !nativeFaulted && SUCCEEDED(colorQuery) &&
				SUCCEEDED(depthQuery) && colorTexture && depthTexture;
		}

		[[nodiscard]] bool ValidPublishedFrame(
			const PublishedFrame& a_frame,
			ID3D11Device* a_device,
			Microsoft::WRL::ComPtr<ID3D11Resource>& a_resource,
			bool& nativeFaulted) noexcept
		{
			using namespace HandMirrorRuntimeBridgePolicy;
			const auto consumer = a_frame.owner.kind == OwnerKind::kWall ?
				PublicationConsumer::kWallPane :
				PublicationConsumer::kInternalHandPane;
			if (!a_frame.valid || !a_frame.colorSRV ||
				!AllowsPublicationConsumer(
					a_frame.owner, a_frame.audience, consumer) ||
				a_frame.captureSequence == 0 || a_frame.width == 0 || a_frame.height == 0 ||
				a_frame.width > kMaximumTextureDimension ||
				a_frame.height > kMaximumTextureDimension ||
				!Finite(a_frame.reflectedOrigin) ||
				!Finite(a_frame.reflectedViewProjection) ||
				!BelongsToDevice(
					a_frame.colorSRV.Get(), a_device, nativeFaulted))
				return false;

			GuardedQueryOwner<ID3D11Texture2D> texture{};
			if (!GetTexture2D(
					a_frame.colorSRV.Get(), a_resource, texture.value,
					nativeFaulted))
				return false;
			D3D11_TEXTURE2D_DESC textureDescription{};
			if (!GetTextureDescriptionSEH(
					texture.value.Get(), textureDescription, nativeFaulted))
				return false;
			// The mirror capture must carry every mip through 1x1.  A partial or
			// single-mip target changes thin vegetation quality with pane angle.
			D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
			if (!GetSRVDescriptionSEH(
					a_frame.colorSRV.Get(), viewDescription, nativeFaulted))
				return false;
			const bool reducedHandAllowed = a_frame.owner.kind == OwnerKind::kHand &&
				HandMirrorLoweredPresentationPolicy::reducedResolutionEnabled.load(std::memory_order_relaxed);
			if (viewDescription.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
				!HandMirrorLoweredPresentationPolicy::ColorViewDimensionsMatch(
					textureDescription.Width, textureDescription.Height, textureDescription.MipLevels,
					viewDescription.Texture2D.MostDetailedMip, viewDescription.Texture2D.MipLevels,
					a_frame.width, a_frame.height, reducedHandAllowed) ||
				textureDescription.ArraySize != 1 ||
				textureDescription.Format != DXGI_FORMAT_R16G16B16A16_FLOAT ||
				textureDescription.SampleDesc.Count != 1 ||
				(textureDescription.BindFlags &
					(D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE)) !=
					(D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE) ||
				(textureDescription.MiscFlags &
					D3D11_RESOURCE_MISC_GENERATE_MIPS) == 0)
				return false;

			return viewDescription.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
		}

		[[nodiscard]] constexpr PaneApertureKind ResolvePaneApertureKind(
			const PublishedFrame& a_frame) noexcept
		{
			if (a_frame.owner.kind !=
					HandMirrorRuntimeBridgePolicy::OwnerKind::kHand ||
				a_frame.audience !=
					HandMirrorRuntimeBridgePolicy::PublicationAudience::kInternalHandOnly) {
				return PaneApertureKind::kWallQuad;
			}

			const auto& surface = a_frame.owner.hand.authoredSurface;
			if (surface.styleIdentity == kFiligreeStyleIdentity &&
				surface.apertureContractIdentity ==
					kFiligreeApertureContractIdentity) {
				return PaneApertureKind::kHandFiligree24;
			}
			// Preserve the exact historical v8 16-gon for every other approved hand
			// owner.  No wall or public-audience frame can enter this branch.
			return PaneApertureKind::kHandRound16;
		}

		// Colour views rejected because they address more than one array slice
		// (D4 stereo guard).  Read through TakeArrayColorTargetRejectionNotice().
		std::atomic<std::uint64_t> g_arrayColorTargetRejects{ 0 };
		std::atomic_bool g_arrayColorTargetRejectNoticeTaken{ false };

		[[nodiscard]] bool IsMultiSliceArrayView(
			const D3D11_RENDER_TARGET_VIEW_DESC& a_description) noexcept
		{
			switch (a_description.ViewDimension) {
			case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
				return a_description.Texture2DArray.ArraySize > 1;
			case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:
				return a_description.Texture2DMSArray.ArraySize > 1;
			default:
				return false;
			}
		}

		[[nodiscard]] bool ValidViewport(const D3D11_VIEWPORT& a_viewport) noexcept
		{
			return Finite(a_viewport.TopLeftX) && Finite(a_viewport.TopLeftY) &&
			       Finite(a_viewport.Width) && Finite(a_viewport.Height) &&
			       Finite(a_viewport.MinDepth) && Finite(a_viewport.MaxDepth) &&
			       a_viewport.TopLeftX >= 0.0f && a_viewport.TopLeftY >= 0.0f &&
			       a_viewport.Width > 0.0f && a_viewport.Height > 0.0f &&
			       a_viewport.Width <= static_cast<float>(kMaximumTextureDimension) &&
			       a_viewport.Height <= static_cast<float>(kMaximumTextureDimension) &&
			       a_viewport.MinDepth >= 0.0f && a_viewport.MaxDepth <= 1.0f &&
			       a_viewport.MinDepth <= a_viewport.MaxDepth;
		}

		[[nodiscard]] DrawStatus ValidateTargets(
			const DrawTargets& a_targets,
			ID3D11Device* a_device,
			ID3D11Resource* a_reflectionResource,
			Microsoft::WRL::ComPtr<ID3D11Resource>& a_colorResource,
			Microsoft::WRL::ComPtr<ID3D11Resource>& a_depthResource,
			bool& nativeFaulted) noexcept
		{
			if (!a_targets.colorRTV)
				return DrawStatus::kColorTargetMissing;
			if (!a_targets.depthDSV)
				return DrawStatus::kDepthTargetMissing;
			if (!ValidViewport(a_targets.viewport))
				return DrawStatus::kViewportRejected;
			if (!BelongsToDevice(
					a_targets.colorRTV, a_device, nativeFaulted) ||
				!BelongsToDevice(
					a_targets.depthDSV, a_device, nativeFaulted))
				return DrawStatus::kTargetDeviceMismatch;

			D3D11_DEPTH_STENCIL_VIEW_DESC depthViewDescription{};
			if (!GetDSVDescriptionSEH(
					a_targets.depthDSV, depthViewDescription, nativeFaulted))
				return DrawStatus::kTargetResourceRejected;
			if ((depthViewDescription.Flags & D3D11_DSV_READ_ONLY_DEPTH) != 0)
				return DrawStatus::kDepthTargetReadOnly;

			// The pane quad is a single non-instanced draw with one viewport and
			// one main view.  Stereo presentation is delivered as one draw per
			// eye into side-by-side rectangles (or a single-eye target); a
			// texture-array colour view with more than one slice would need
			// SV_RenderTargetArrayIndex routing the shader does not perform, so
			// the slices beyond the first would silently stay dark.  Fail closed.
			D3D11_RENDER_TARGET_VIEW_DESC colorViewDescription{};
			if (!GetRTVDescriptionSEH(
					a_targets.colorRTV, colorViewDescription, nativeFaulted))
				return DrawStatus::kTargetResourceRejected;
			if (IsMultiSliceArrayView(colorViewDescription)) {
				g_arrayColorTargetRejects.fetch_add(1, std::memory_order_relaxed);
				return DrawStatus::kTargetShapeRejected;
			}

			GuardedQueryOwner<ID3D11Texture2D> colorTexture{};
			GuardedQueryOwner<ID3D11Texture2D> depthTexture{};
			if (!GetTexture2D(
					a_targets.colorRTV, a_colorResource, colorTexture.value,
					nativeFaulted) ||
				!GetTexture2D(
					a_targets.depthDSV, a_depthResource, depthTexture.value,
					nativeFaulted))
				return DrawStatus::kTargetResourceRejected;
			if (a_colorResource.Get() == a_depthResource.Get() ||
				a_colorResource.Get() == a_reflectionResource ||
				a_depthResource.Get() == a_reflectionResource)
				return DrawStatus::kTargetAliasRejected;

			D3D11_TEXTURE2D_DESC colorDescription{};
			D3D11_TEXTURE2D_DESC depthDescription{};
			if (!GetTextureDescriptionSEH(
					colorTexture.value.Get(), colorDescription, nativeFaulted) ||
				!GetTextureDescriptionSEH(
					depthTexture.value.Get(), depthDescription, nativeFaulted))
				return DrawStatus::kTargetResourceRejected;
			if (colorDescription.Width == 0 || colorDescription.Height == 0 ||
				colorDescription.Width != depthDescription.Width ||
				colorDescription.Height != depthDescription.Height ||
				colorDescription.SampleDesc.Count != depthDescription.SampleDesc.Count ||
				colorDescription.SampleDesc.Quality != depthDescription.SampleDesc.Quality)
				return DrawStatus::kTargetShapeRejected;
			if ((colorDescription.BindFlags & D3D11_BIND_RENDER_TARGET) == 0 ||
				(depthDescription.BindFlags & D3D11_BIND_DEPTH_STENCIL) == 0)
				return DrawStatus::kTargetBindFlagsRejected;

			const float right = a_targets.viewport.TopLeftX + a_targets.viewport.Width;
			const float bottom = a_targets.viewport.TopLeftY + a_targets.viewport.Height;
			if (!Finite(right) || !Finite(bottom) ||
				right > static_cast<float>(colorDescription.Width) ||
				bottom > static_cast<float>(colorDescription.Height))
				return DrawStatus::kViewportRejected;
			return DrawStatus::kDrawn;
		}

		[[nodiscard]] bool ValidateMotionTarget(
			ID3D11RenderTargetView* a_motionRTV,
			ID3D11Device* a_device,
			ID3D11Resource* a_reflectionResource,
			ID3D11Resource* a_colorResource,
			ID3D11Resource* a_depthResource,
			Microsoft::WRL::ComPtr<ID3D11Resource>& a_motionResource,
			bool& nativeFaulted) noexcept
		{
			if (!a_motionRTV || !BelongsToDevice(
					a_motionRTV, a_device, nativeFaulted))
				return false;
			GuardedQueryOwner<ID3D11Texture2D> motionTexture{};
			if (!GetTexture2D(
					a_motionRTV, a_motionResource, motionTexture.value,
					nativeFaulted) ||
				a_motionResource.Get() == a_reflectionResource ||
				a_motionResource.Get() == a_colorResource ||
				a_motionResource.Get() == a_depthResource)
				return false;

			GuardedQueryOwner<ID3D11Texture2D> colorTexture{};
			GuardedQueryOwner<ID3D11Texture2D> depthTexture{};
			ID3D11Texture2D* rawColorTexture = nullptr;
			ID3D11Texture2D* rawDepthTexture = nullptr;
			const bool texturePairValid = QueryTexturePairSEH(
				a_colorResource, a_depthResource, rawColorTexture,
				rawDepthTexture, nativeFaulted);
			colorTexture.value.Attach(rawColorTexture);
			depthTexture.value.Attach(rawDepthTexture);
			if (!texturePairValid || !colorTexture.value || !depthTexture.value)
				return false;

			D3D11_TEXTURE2D_DESC motionDescription{};
			D3D11_TEXTURE2D_DESC colorDescription{};
			D3D11_TEXTURE2D_DESC depthDescription{};
			if (!GetTextureDescriptionSEH(
					motionTexture.value.Get(), motionDescription, nativeFaulted) ||
				!GetTextureDescriptionSEH(
					colorTexture.value.Get(), colorDescription, nativeFaulted) ||
				!GetTextureDescriptionSEH(
					depthTexture.value.Get(), depthDescription, nativeFaulted))
				return false;
			if (motionDescription.Width != colorDescription.Width ||
				motionDescription.Height != colorDescription.Height ||
				motionDescription.Width != depthDescription.Width ||
				motionDescription.Height != depthDescription.Height ||
				motionDescription.Format != DXGI_FORMAT_R16G16_FLOAT ||
				motionDescription.SampleDesc.Count != colorDescription.SampleDesc.Count ||
				motionDescription.SampleDesc.Quality != colorDescription.SampleDesc.Quality ||
				motionDescription.SampleDesc.Count != depthDescription.SampleDesc.Count ||
				motionDescription.SampleDesc.Quality != depthDescription.SampleDesc.Quality ||
				(motionDescription.BindFlags & D3D11_BIND_RENDER_TARGET) == 0)
				return false;

			D3D11_RENDER_TARGET_VIEW_DESC viewDescription{};
			if (!GetRTVDescriptionSEH(
					a_motionRTV, viewDescription, nativeFaulted))
				return false;
			return viewDescription.Format == DXGI_FORMAT_R16G16_FLOAT &&
				(viewDescription.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D ||
				 viewDescription.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2DMS);
		}

		[[nodiscard]] bool ValidateMotionDepth(
			ID3D11ShaderResourceView* a_depthSRV,
			ID3D11Device* a_device,
			std::uint32_t a_width,
			std::uint32_t a_height,
			bool a_reducedHandAllowed,
			ID3D11Resource* a_reflectionResource,
			ID3D11Resource* a_colorTargetResource,
			ID3D11Resource* a_depthTargetResource,
			ID3D11Resource* a_motionTargetResource,
			Microsoft::WRL::ComPtr<ID3D11Resource>& a_depthResource,
			bool& nativeFaulted) noexcept
		{
			if (!a_depthSRV || a_width == 0 || a_height == 0 ||
				!BelongsToDevice(a_depthSRV, a_device, nativeFaulted)) {
				return false;
			}

			GuardedQueryOwner<ID3D11Texture2D> depthTexture{};
			if (!GetTexture2D(
					a_depthSRV, a_depthResource, depthTexture.value,
					nativeFaulted) ||
				a_depthResource.Get() == a_reflectionResource ||
				a_depthResource.Get() == a_colorTargetResource ||
				a_depthResource.Get() == a_depthTargetResource ||
				a_depthResource.Get() == a_motionTargetResource) {
				return false;
			}

			D3D11_TEXTURE2D_DESC textureDescription{};
			D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
			if (!GetTextureDescriptionSEH(
					depthTexture.value.Get(), textureDescription, nativeFaulted) ||
				!GetSRVDescriptionSEH(
					a_depthSRV, viewDescription, nativeFaulted)) {
				return false;
			}
			return viewDescription.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D &&
			       HandMirrorLoweredPresentationPolicy::DepthViewDimensionsMatch(
				       textureDescription.Width, textureDescription.Height, textureDescription.MipLevels,
				       viewDescription.Texture2D.MostDetailedMip, viewDescription.Texture2D.MipLevels,
				       a_width, a_height, a_reducedHandAllowed) &&
			       textureDescription.ArraySize == 1 &&
			       textureDescription.Format == DXGI_FORMAT_R24G8_TYPELESS &&
			       textureDescription.SampleDesc.Count == 1 &&
			       (textureDescription.BindFlags &
					   (D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE)) ==
					   (D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE) &&
			       viewDescription.ViewDimension ==
					   D3D11_SRV_DIMENSION_TEXTURE2D &&
			       viewDescription.Format == DXGI_FORMAT_R24_UNORM_X8_TYPELESS &&
			       viewDescription.Texture2D.MipLevels == 1;
		}

		[[nodiscard]] DirectX::XMFLOAT3 WorldCorner(
			const PaneTransform& a_pane,
			float a_x,
			float a_y) noexcept
		{
			return {
				a_pane.center.x + a_x * a_pane.tangentExtent.x +
					a_y * a_pane.bitangentExtent.x,
				a_pane.center.y + a_x * a_pane.tangentExtent.y +
					a_y * a_pane.bitangentExtent.y,
				a_pane.center.z + a_x * a_pane.tangentExtent.z +
					a_y * a_pane.bitangentExtent.z
			};
		}

		[[nodiscard]] DirectX::XMFLOAT4 ProjectRelative(
			const DirectX::XMFLOAT3& a_world,
			const DirectX::XMFLOAT3& a_origin,
			const DirectX::XMFLOAT4X4& a_viewProjection) noexcept
		{
			const DirectX::XMVECTOR relative = DirectX::XMVectorSet(
				a_world.x - a_origin.x,
				a_world.y - a_origin.y,
				a_world.z - a_origin.z,
				1.0f);
			const DirectX::XMVECTOR clip = DirectX::XMVector4Transform(
				relative, DirectX::XMLoadFloat4x4(&a_viewProjection));
			DirectX::XMFLOAT4 output{};
			DirectX::XMStoreFloat4(&output, clip);
			return output;
		}

		[[nodiscard]] ProjectionCoverage EvaluateProjectionCoverage(
			const PaneTransform& a_pane,
			const MainView& a_mainView,
			const PublishedFrame& a_frame,
			std::span<const DirectX::XMFLOAT2> customPolygon = {}) noexcept
		{
			if (!ValidPane(a_pane) || !Finite(a_mainView.origin) ||
				!Finite(a_mainView.viewProjection))
				return ProjectionCoverage::kRejected;

			struct ProjectedVertex
			{
				DirectX::XMFLOAT2 unit{};
				DirectX::XMFLOAT4 mainClip{};
				DirectX::XMFLOAT4 reflectedClip{};
			};
			enum class ClipPlane : std::uint8_t
			{
				kMainPositiveW,
				kMainLeft,
				kMainRight,
				kMainBottom,
				kMainTop,
				kMainNear,
				kMainFar,
				kReflectedPositiveW,
				kReflectedLeft,
				kReflectedRight,
				kReflectedBottom,
				kReflectedTop
			};
			constexpr std::array mainPlanes{
				ClipPlane::kMainPositiveW,
				ClipPlane::kMainLeft,
				ClipPlane::kMainRight,
				ClipPlane::kMainBottom,
				ClipPlane::kMainTop,
				ClipPlane::kMainNear,
				ClipPlane::kMainFar
			};
			constexpr std::array reflectedPlanes{
				ClipPlane::kReflectedPositiveW,
				ClipPlane::kReflectedLeft,
				ClipPlane::kReflectedRight,
				ClipPlane::kReflectedBottom,
				ClipPlane::kReflectedTop
			};
			const auto planeValue = [](const ProjectedVertex& a_vertex,
				ClipPlane a_plane) noexcept {
				const auto& main = a_vertex.mainClip;
				const auto& reflected = a_vertex.reflectedClip;
				switch (a_plane) {
				case ClipPlane::kMainPositiveW:
					return main.w - kProjectionEpsilon;
				case ClipPlane::kMainLeft:
					return main.x + main.w;
				case ClipPlane::kMainRight:
					return main.w - main.x;
				case ClipPlane::kMainBottom:
					return main.y + main.w;
				case ClipPlane::kMainTop:
					return main.w - main.y;
				case ClipPlane::kMainNear:
					return main.z;
				case ClipPlane::kMainFar:
					return main.w - main.z;
				case ClipPlane::kReflectedPositiveW:
					return reflected.w - kProjectionEpsilon;
				case ClipPlane::kReflectedLeft:
					return reflected.x + reflected.w;
				case ClipPlane::kReflectedRight:
					return reflected.w - reflected.x;
				case ClipPlane::kReflectedBottom:
					return reflected.y + reflected.w;
				case ClipPlane::kReflectedTop:
					return reflected.w - reflected.y;
				default:
					return -1.0f;
				}
			};
			const auto interpolate = [](const ProjectedVertex& a_left,
				const ProjectedVertex& a_right, float a_t) noexcept {
				const auto lerp = [a_t](float a_start, float a_end) noexcept {
					return a_start + a_t * (a_end - a_start);
				};
				return ProjectedVertex{
					{ lerp(a_left.unit.x, a_right.unit.x),
						lerp(a_left.unit.y, a_right.unit.y) },
					{ lerp(a_left.mainClip.x, a_right.mainClip.x),
						lerp(a_left.mainClip.y, a_right.mainClip.y),
						lerp(a_left.mainClip.z, a_right.mainClip.z),
						lerp(a_left.mainClip.w, a_right.mainClip.w) },
					{ lerp(a_left.reflectedClip.x, a_right.reflectedClip.x),
						lerp(a_left.reflectedClip.y, a_right.reflectedClip.y),
						lerp(a_left.reflectedClip.z, a_right.reflectedClip.z),
						lerp(a_left.reflectedClip.w, a_right.reflectedClip.w) }
				};
			};

			constexpr std::array<DirectX::XMFLOAT2, 4> wallUnits{
				DirectX::XMFLOAT2{ -1.0f, -1.0f },
				DirectX::XMFLOAT2{ +1.0f, -1.0f },
				DirectX::XMFLOAT2{ +1.0f, +1.0f },
				DirectX::XMFLOAT2{ -1.0f, +1.0f }
			};
			// A convex clip can add at most one vertex per clip plane.  Forty-eight
			// leaves bounded headroom for the 24-edge filigree aperture through both
			// the main and reflected six-plane clip volumes.
			std::array<ProjectedVertex, 48> polygon{};
			const auto apertureKind = ResolvePaneApertureKind(a_frame);
			const Vertex* handVertices = nullptr;
			std::size_t handVertexCount = 0;
			switch (apertureKind) {
			case PaneApertureKind::kHandRound16:
				handVertices = kHandRound16Vertices.data();
				handVertexCount = kHandRound16Vertices.size();
				break;
			case PaneApertureKind::kHandFiligree24:
				handVertices = kHandFiligree24Vertices.data();
				handVertexCount = kHandFiligree24Vertices.size();
				break;
			case PaneApertureKind::kWallQuad:
			default:
				break;
			}
			const bool authoredHandAperture = handVertices != nullptr;
			const std::size_t polygonSizeBeforeProjection = !customPolygon.empty() ? customPolygon.size() :
				(authoredHandAperture ? handVertexCount - 1 : wallUnits.size());
			if (polygonSizeBeforeProjection > 24) return ProjectionCoverage::kRejected;
			std::size_t polygonSize = polygonSizeBeforeProjection;
			for (std::size_t index = 0; index < polygonSizeBeforeProjection; ++index) {
				const auto unit = !customPolygon.empty() ? customPolygon[index] :
					(authoredHandAperture ? handVertices[index + 1].unitPosition : wallUnits[index]);
				const DirectX::XMFLOAT3 world = WorldCorner(a_pane, unit.x, unit.y);
				const DirectX::XMFLOAT4 mainClip = ProjectRelative(
					world, a_mainView.origin, a_mainView.viewProjection);
				const DirectX::XMFLOAT4 reflectedClip = ProjectRelative(
					world, a_frame.reflectedOrigin, a_frame.reflectedViewProjection);
				if (!Finite(mainClip) || !Finite(reflectedClip))
					return ProjectionCoverage::kRejected;
				polygon[index] = { unit, mainClip, reflectedClip };
			}

			bool clippingInvalid = false;
			const auto clipPolygon = [&polygon, &polygonSize, &planeValue,
				&interpolate, &clippingInvalid](ClipPlane a_plane) noexcept {
				if (polygonSize < 3)
					return false;
				std::array<ProjectedVertex, 48> output{};
				std::size_t outputSize = 0;
				ProjectedVertex previous = polygon[polygonSize - 1];
				float previousValue = planeValue(previous, a_plane);
				bool previousInside = previousValue >= 0.0f;
				for (std::size_t index = 0; index < polygonSize; ++index) {
					const ProjectedVertex current = polygon[index];
					const float currentValue = planeValue(current, a_plane);
					if (!Finite(currentValue) || !Finite(previousValue)) {
						clippingInvalid = true;
						return false;
					}
					const bool currentInside = currentValue >= 0.0f;
					if (currentInside != previousInside) {
						const float denominator = previousValue - currentValue;
						if (!Finite(denominator) || std::abs(denominator) <=
							std::numeric_limits<float>::min()) {
							clippingInvalid = true;
							return false;
						}
						const float t = previousValue / denominator;
						if (!Finite(t) || t < 0.0f || t > 1.0f || outputSize >= output.size()) {
							clippingInvalid = true;
							return false;
						}
						output[outputSize++] = interpolate(previous, current, t);
					}
					if (currentInside) {
						if (outputSize >= output.size()) {
							clippingInvalid = true;
							return false;
						}
						output[outputSize++] = current;
					}
					previous = current;
					previousValue = currentValue;
					previousInside = currentInside;
				}
				polygon = output;
				polygonSize = outputSize;
				return polygonSize >= 3;
			};

			bool projectedAreaInvalid = false;
			const auto hasArea = [&polygon, &polygonSize,
				&projectedAreaInvalid]() noexcept {
				if (polygonSize < 3)
					return false;
				float twiceArea = 0.0f;
				for (std::size_t index = 0; index < polygonSize; ++index) {
					const auto& currentClip = polygon[index].mainClip;
					const auto& nextClip =
						polygon[(index + 1) % polygonSize].mainClip;
					if (!Finite(currentClip.w) || !Finite(nextClip.w) ||
						currentClip.w < kProjectionEpsilon ||
						nextClip.w < kProjectionEpsilon) {
						projectedAreaInvalid = true;
						return false;
					}
					const float currentX = currentClip.x / currentClip.w;
					const float currentY = currentClip.y / currentClip.w;
					const float nextX = nextClip.x / nextClip.w;
					const float nextY = nextClip.y / nextClip.w;
					if (!Finite(currentX) || !Finite(currentY) ||
						!Finite(nextX) || !Finite(nextY)) {
						projectedAreaInvalid = true;
						return false;
					}
					twiceArea += currentX * nextY - currentY * nextX;
				}
				if (!Finite(twiceArea)) {
					projectedAreaInvalid = true;
					return false;
				}
				return std::abs(twiceArea) > kMinimumProjectedArea;
			};

			// First isolate the exact part of the authored quad which D3D can rasterize
			// in the restored main view (LH standard-Z homogeneous frustum).
			for (const auto plane : mainPlanes) {
				if (!clipPolygon(plane)) {
					return clippingInvalid ? ProjectionCoverage::kRejected :
						ProjectionCoverage::kMainViewNotVisible;
				}
			}
			if (!hasArea()) {
				return projectedAreaInvalid ? ProjectionCoverage::kRejected :
					ProjectionCoverage::kMainViewNotVisible;
			}

			// Suppression is safe only if every main-visible point is also covered by
			// the reflected target.  Convex homogeneous half-spaces make checking the
			// clipped polygon vertices sufficient for the entire visible region.
			bool fullCoverage = true;
			for (std::size_t index = 0; index < polygonSize && fullCoverage; ++index) {
				for (const auto plane : reflectedPlanes) {
					const float value = planeValue(polygon[index], plane);
					if (!Finite(value))
						return ProjectionCoverage::kRejected;
					if (value < 0.0f) {
						fullCoverage = false;
						break;
					}
				}
			}
			if (fullCoverage)
				return ProjectionCoverage::kFull;

			// Keep a partially covered pane drawable: the PS discards invalid reflected
			// fragments and M5 retains/restores the authored fallback underneath them.
			for (const auto plane : reflectedPlanes) {
				if (!clipPolygon(plane)) {
					return clippingInvalid ? ProjectionCoverage::kRejected :
						ProjectionCoverage::kReflectedUncovered;
				}
			}
			if (hasArea())
				return ProjectionCoverage::kPartial;
			return projectedAreaInvalid ? ProjectionCoverage::kRejected :
				ProjectionCoverage::kReflectedUncovered;
		}

		[[nodiscard]] bool AnyStreamOutputTargetBound(
			ID3D11DeviceContext* a_context,
			bool& nativeFaulted) noexcept
		{
			std::array<ID3D11Buffer*, D3D11_SO_BUFFER_SLOT_COUNT> rawTargets{};
			__try {
				a_context->SOGetTargets(
					static_cast<UINT>(rawTargets.size()), rawTargets.data());
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				nativeFaulted = true;
			}
			bool anyBound = false;
			for (auto*& target : rawTargets) {
				if (target) {
					anyBound = true;
					bool releaseFaulted = false;
					ReleaseQueryInterfaceSEH(target, releaseFaulted);
					nativeFaulted = nativeFaulted || releaseFaulted;
				}
			}
			return anyBound && !nativeFaulted;
		}

		[[nodiscard]] bool ViewAliasesOutput(
			ID3D11View* a_view,
			ID3D11Resource* a_colorResource,
			ID3D11Resource* a_depthResource,
			ID3D11Resource* a_motionResource,
			bool a_allowColorDepthAlias,
			bool& nativeFaulted) noexcept
		{
			if (!a_view || nativeFaulted)
				return false;
			ID3D11Resource* resource = nullptr;
			__try {
				a_view->GetResource(&resource);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				nativeFaulted = true;
			}
			const bool aliasesColorDepth =
				resource == a_colorResource || resource == a_depthResource;
			const bool aliasesMotion =
				a_motionResource && resource == a_motionResource;
			const bool aliases = aliasesMotion ||
				(!a_allowColorDepthAlias && aliasesColorDepth);
			bool releaseFaulted = false;
			ReleaseQueryInterfaceSEH(resource, releaseFaulted);
			nativeFaulted = nativeFaulted || releaseFaulted;
			return aliases && !nativeFaulted;
		}

		template <class Getter>
		[[nodiscard]] bool ShaderResourceAliasBound(
			Getter&& a_getter,
			ID3D11Resource* a_colorResource,
			ID3D11Resource* a_depthResource,
			ID3D11Resource* a_motionResource,
			bool& nativeFaulted,
			UINT a_allowedSlot = (std::numeric_limits<UINT>::max)()) noexcept
		{
			std::array<ID3D11ShaderResourceView*,
				D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> views{};
			__try {
				a_getter(0, static_cast<UINT>(views.size()), views.data());
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				nativeFaulted = true;
			}
			bool aliasBound = false;
			for (UINT index = 0; index < views.size(); ++index) {
				if (ViewAliasesOutput(
						views[index], a_colorResource, a_depthResource,
						a_motionResource, index == a_allowedSlot,
						nativeFaulted))
					aliasBound = true;
				bool releaseFaulted = false;
				ReleaseQueryInterfaceSEH(views[index], releaseFaulted);
				nativeFaulted = nativeFaulted || releaseFaulted;
			}
			return aliasBound && !nativeFaulted;
		}

		[[nodiscard]] bool ComputeUAVAliasBound(
			ID3D11DeviceContext* a_context,
			ID3D11Resource* a_colorResource,
			ID3D11Resource* a_depthResource,
			ID3D11Resource* a_motionResource,
			bool& nativeFaulted) noexcept
		{
			std::array<ID3D11UnorderedAccessView*,
				D3D11_PS_CS_UAV_REGISTER_COUNT> computeViews{};
			__try {
				a_context->CSGetUnorderedAccessViews(
					0, static_cast<UINT>(computeViews.size()), computeViews.data());
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				nativeFaulted = true;
			}
			bool aliasBound = false;
			for (auto*& view : computeViews) {
				if (ViewAliasesOutput(
						view, a_colorResource, a_depthResource, a_motionResource,
						false, nativeFaulted))
					aliasBound = true;
				bool releaseFaulted = false;
				ReleaseQueryInterfaceSEH(view, releaseFaulted);
				nativeFaulted = nativeFaulted || releaseFaulted;
			}
			return aliasBound && !nativeFaulted;
		}

		[[nodiscard]] DrawStatus ValidateUntrackedOutputs(
			ID3D11DeviceContext* a_context,
			ID3D11Resource* a_colorResource,
			ID3D11Resource* a_depthResource,
			ID3D11Resource* a_motionResource,
			bool& nativeFaulted) noexcept
		{
			const auto checkShaderResources =
				[&](auto&& getter, const UINT allowedSlot =
					(std::numeric_limits<UINT>::max)()) noexcept {
					const bool alias = ShaderResourceAliasBound(
						std::forward<decltype(getter)>(getter), a_colorResource,
						a_depthResource, a_motionResource, nativeFaulted,
						allowedSlot);
					if (nativeFaulted)
						return DrawStatus::kContextStateCaptureFailed;
					return alias ? DrawStatus::kShaderResourceAliasBound :
						DrawStatus::kDrawn;
				};
			DrawStatus shaderStatus = checkShaderResources(
				[a_context](UINT start, UINT count, ID3D11ShaderResourceView** views) {
					a_context->VSGetShaderResources(start, count, views);
				});
			if (shaderStatus != DrawStatus::kDrawn)
				return shaderStatus;
			shaderStatus = checkShaderResources(
				[a_context](UINT start, UINT count, ID3D11ShaderResourceView** views) {
					a_context->GSGetShaderResources(start, count, views);
				});
			if (shaderStatus != DrawStatus::kDrawn)
				return shaderStatus;
			shaderStatus = checkShaderResources(
				[a_context](UINT start, UINT count, ID3D11ShaderResourceView** views) {
					a_context->HSGetShaderResources(start, count, views);
				});
			if (shaderStatus != DrawStatus::kDrawn)
				return shaderStatus;
			shaderStatus = checkShaderResources(
				[a_context](UINT start, UINT count, ID3D11ShaderResourceView** views) {
					a_context->DSGetShaderResources(start, count, views);
				});
			if (shaderStatus != DrawStatus::kDrawn)
				return shaderStatus;
			shaderStatus = checkShaderResources(
				[a_context](UINT start, UINT count, ID3D11ShaderResourceView** views) {
					a_context->CSGetShaderResources(start, count, views);
				});
			if (shaderStatus != DrawStatus::kDrawn)
				return shaderStatus;
			// PS t0 is the one legal alias: it is explicitly retained/restored by
			// ContextStateSnapshot after the custom OM target is removed.
			shaderStatus = checkShaderResources(
				[a_context](UINT start, UINT count, ID3D11ShaderResourceView** views) {
					a_context->PSGetShaderResources(start, count, views);
				}, 0);
			if (shaderStatus != DrawStatus::kDrawn)
				return shaderStatus;

			if (ComputeUAVAliasBound(
					a_context, a_colorResource, a_depthResource, a_motionResource,
					nativeFaulted))
				return DrawStatus::kComputeUAVAliasBound;
			if (nativeFaulted)
				return DrawStatus::kContextStateCaptureFailed;

			// OM UAV append/counter state cannot be reconstructed. The helper
			// checks all 8 FL11.0 slots or all 64 FL11.1 slots before mutation.
			const auto outputMergerUAVState =
				D3D11OutputMergerState::QueryUAVState(a_context);
			if (outputMergerUAVState ==
				D3D11OutputMergerState::UAVState::kFault) {
				nativeFaulted = true;
				return DrawStatus::kContextStateCaptureFailed;
			}
			if (outputMergerUAVState ==
				D3D11OutputMergerState::UAVState::kBound)
				return DrawStatus::kOutputMergerUAVUnsafe;
			return DrawStatus::kDrawn;
		}

		template <class Shader>
		struct ShaderStageSnapshot
		{
			Shader* shader{ nullptr };
			std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> instances{};
			UINT instanceCount{ 0 };
		};

		struct RawContextStateSnapshot
		{
			ID3D11DeviceContext* context{ nullptr };  // synchronously borrowed
			ID3D11InputLayout* inputLayout{ nullptr };
			ID3D11Buffer* vertexBuffer{ nullptr };
			UINT vertexStride{ 0 };
			UINT vertexOffset{ 0 };
			ID3D11Buffer* indexBuffer{ nullptr };
			DXGI_FORMAT indexFormat{ DXGI_FORMAT_UNKNOWN };
			UINT indexOffset{ 0 };
			D3D11_PRIMITIVE_TOPOLOGY topology{ D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED };
			ShaderStageSnapshot<ID3D11VertexShader> vertexStage{};
			ShaderStageSnapshot<ID3D11PixelShader> pixelStage{};
			ShaderStageSnapshot<ID3D11GeometryShader> geometryStage{};
			ShaderStageSnapshot<ID3D11HullShader> hullStage{};
			ShaderStageSnapshot<ID3D11DomainShader> domainStage{};
			ID3D11Buffer* vertexConstantBuffer{ nullptr };
			ID3D11Buffer* pixelConstantBuffer{ nullptr };
			std::array<ID3D11ShaderResourceView*, 2> pixelShaderResources{};
			ID3D11SamplerState* pixelSampler{ nullptr };
			ID3D11RasterizerState* rasterizerState{ nullptr };
			std::array<D3D11_VIEWPORT,
				D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports{};
			UINT viewportCount{ 0 };
			ID3D11BlendState* blendState{ nullptr };
			std::array<float, 4> blendFactor{};
			UINT sampleMask{ 0 };
			ID3D11DepthStencilState* depthStencilState{ nullptr };
			UINT stencilReference{ 0 };
			std::array<ID3D11RenderTargetView*,
				D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> renderTargets{};
			ID3D11DepthStencilView* depthStencilView{ nullptr };
			ID3D11Predicate* predicate{ nullptr };
			BOOL predicateValue{ FALSE };
			// False when the hull/domain stages were deliberately not queried
			// (forwarding device): they are then neither bound nor restored.
			bool tessellationCaptured{ true };
		};
		static_assert(std::is_trivially_destructible_v<RawContextStateSnapshot>);

		/**
		 * Skyrim never binds hull or domain shaders and this renderer never uses
		 * them.  Through a forwarding (ENB) context the HS/DS getters are the only
		 * state queries no coexisting plugin ever issues; the 2026-09-10 AE run
		 * recorded exactly two access violations inside the snapshot's releases,
		 * so under an accepted forwarding device these two stages are left
		 * untouched entirely: not queried, not nulled, not restored.
		 */
		[[nodiscard]] bool TouchTessellationStages() noexcept
		{
			return !EngineDeviceIdentity::ForwardingDeviceAccepted();
		}

		enum class RestoreStep : std::uint8_t
		{
			kUnbindPrivateResources,
			kInputLayout,
			kVertexBuffer,
			kIndexBuffer,
			kTopology,
			kVertexShader,
			kPixelShader,
			kGeometryShader,
			kHullShader,
			kDomainShader,
			kVertexConstantBuffer,
			kPixelConstantBuffer,
			kPixelSampler,
			kRasterizerState,
			kViewports,
			kBlendState,
			kDepthStencilState,
			kRenderTargets,
			kPixelResources,
			kPredication,
			kCount
		};
		static_assert(static_cast<std::uint32_t>(RestoreStep::kCount) == 20);

		template <class Shader, class Getter>
		void CaptureShaderStage(
			ShaderStageSnapshot<Shader>& stage,
			Getter&& getter) noexcept
		{
			UINT count = static_cast<UINT>(stage.instances.size());
			getter(&stage.shader, stage.instances.data(), &count);
			stage.instanceCount =
				(std::min)(count, static_cast<UINT>(stage.instances.size()));
		}

		template <class Shader, class Setter>
		void RestoreShaderStage(
			const ShaderStageSnapshot<Shader>& stage,
			Setter&& setter) noexcept
		{
			setter(
				stage.shader,
				stage.instanceCount != 0 ? stage.instances.data() : nullptr,
				stage.instanceCount);
		}

		void CaptureContextStateUnsafe(
			ID3D11DeviceContext* context,
			RawContextStateSnapshot* state) noexcept
		{
			state->context = context;
			context->IAGetInputLayout(&state->inputLayout);
			context->IAGetVertexBuffers(
				0, 1, &state->vertexBuffer, &state->vertexStride,
				&state->vertexOffset);
			context->IAGetIndexBuffer(
				&state->indexBuffer, &state->indexFormat, &state->indexOffset);
			context->IAGetPrimitiveTopology(&state->topology);

			CaptureShaderStage(state->vertexStage,
				[context](ID3D11VertexShader** shader,
					ID3D11ClassInstance** instances, UINT* count) {
					context->VSGetShader(shader, instances, count);
				});
			CaptureShaderStage(state->pixelStage,
				[context](ID3D11PixelShader** shader,
					ID3D11ClassInstance** instances, UINT* count) {
					context->PSGetShader(shader, instances, count);
				});
			CaptureShaderStage(state->geometryStage,
				[context](ID3D11GeometryShader** shader,
					ID3D11ClassInstance** instances, UINT* count) {
					context->GSGetShader(shader, instances, count);
				});
			state->tessellationCaptured = TouchTessellationStages();
			if (state->tessellationCaptured) {
				CaptureShaderStage(state->hullStage,
					[context](ID3D11HullShader** shader,
						ID3D11ClassInstance** instances, UINT* count) {
						context->HSGetShader(shader, instances, count);
					});
				CaptureShaderStage(state->domainStage,
					[context](ID3D11DomainShader** shader,
						ID3D11ClassInstance** instances, UINT* count) {
						context->DSGetShader(shader, instances, count);
					});
			}

			context->VSGetConstantBuffers(0, 1, &state->vertexConstantBuffer);
			context->PSGetConstantBuffers(0, 1, &state->pixelConstantBuffer);
			context->PSGetShaderResources(
				0, static_cast<UINT>(state->pixelShaderResources.size()),
				state->pixelShaderResources.data());
			context->PSGetSamplers(0, 1, &state->pixelSampler);
			context->RSGetState(&state->rasterizerState);
			state->viewportCount = static_cast<UINT>(state->viewports.size());
			context->RSGetViewports(&state->viewportCount, state->viewports.data());
			context->OMGetBlendState(
				&state->blendState, state->blendFactor.data(), &state->sampleMask);
			context->OMGetDepthStencilState(
				&state->depthStencilState, &state->stencilReference);
			context->OMGetRenderTargets(
				static_cast<UINT>(state->renderTargets.size()),
				state->renderTargets.data(), &state->depthStencilView);
			context->GetPredication(&state->predicate, &state->predicateValue);
		}

		__declspec(noinline) bool CaptureContextStateSEH(
			ID3D11DeviceContext* context,
			RawContextStateSnapshot* state) noexcept
		{
			bool captured = false;
			__try {
				CaptureContextStateUnsafe(context, state);
				captured = true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				captured = false;
			}
			return captured;
		}

		enum class ReleaseDomain : std::uint8_t
		{
			kSnapshot,
			kShutdown
		};

		std::atomic<std::uint32_t> g_restoreFaults{ 0 };
		std::atomic<std::uint32_t> g_restoreFaultCode{ 0 };
		std::atomic<std::uint8_t> g_restoreFaultStep{ 0xFF };
		std::atomic<std::uint8_t> g_firstFaultKind{ 0xFF };
		std::atomic<std::uint8_t> g_firstFaultIndex{ 0xFF };
		std::atomic<std::uint32_t> g_firstFaultCode{ 0 };
		std::atomic<std::uintptr_t> g_firstFaultPointer{ 0 };
		std::atomic<std::uintptr_t> g_firstFaultModule{ 0 };
		std::atomic<std::uint32_t> g_releaseOrdinalMask{ 0 };
		std::atomic<std::uint32_t> g_restoreStepMask{ 0 };
		std::atomic<DiagnosticSink> g_diagnosticSink{ nullptr };

		void Emit(const char* message) noexcept
		{
			if (auto* sink = g_diagnosticSink.load(std::memory_order_acquire))
				sink(message);
		}

		[[nodiscard]] bool ReadableCommitted(const void* address, std::size_t bytes) noexcept
		{
			if (!address)
				return false;
			MEMORY_BASIC_INFORMATION info{};
			if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info))
				return false;
			if (info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD) != 0 ||
				(info.Protect & PAGE_NOACCESS) != 0)
				return false;
			const auto begin = reinterpret_cast<std::uintptr_t>(address);
			const auto regionEnd = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
			return begin + bytes <= regionEnd;
		}

		[[nodiscard]] std::uintptr_t ImageContaining(const void* address) noexcept
		{
			HMODULE module = nullptr;
			if (!address ||
				!GetModuleHandleExW(
					GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
						GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(address), &module)) {
				return 0;
			}
			return reinterpret_cast<std::uintptr_t>(module);
		}

		/** 0 null, 1 unreadable, 2 vtable unreadable, 3 not in an image, 4 callable. */
		[[nodiscard]] std::uint8_t InspectComObject(
			const void* object, std::uintptr_t& releaseModule) noexcept
		{
			releaseModule = 0;
			if (!object)
				return 0;
			if (!ReadableCommitted(object, sizeof(void*)))
				return 1;
			const void* vtable = *static_cast<const void* const*>(object);
			if (!ReadableCommitted(vtable, 3 * sizeof(void*)))
				return 2;
			const void* release = static_cast<const void* const*>(vtable)[2];
			releaseModule = ImageContaining(release);
			return releaseModule != 0 ? 4 : 3;
		}

		[[nodiscard]] int RecordRestoreFault(
			const std::uint8_t kind, const std::uint8_t index,
			const void* pointer, const unsigned long code) noexcept
		{
			const auto ordinal = g_restoreFaults.fetch_add(1, std::memory_order_relaxed);
			g_restoreFaultCode.store(static_cast<std::uint32_t>(code), std::memory_order_relaxed);
			g_restoreFaultStep.store(
				kind == 0 ? index : (kind == 1 ? std::uint8_t{ 0xFE } : std::uint8_t{ 0xFD }),
				std::memory_order_relaxed);
			if (kind == 0)
				g_restoreStepMask.fetch_or(1u << (index & 31u), std::memory_order_relaxed);
			else if (kind == 1)
				g_releaseOrdinalMask.fetch_or(1u << (index & 31u), std::memory_order_relaxed);
			if (ordinal == 0) {
				std::uintptr_t module = 0;
				(void)InspectComObject(pointer, module);
				g_firstFaultKind.store(kind, std::memory_order_relaxed);
				g_firstFaultIndex.store(index, std::memory_order_relaxed);
				g_firstFaultCode.store(static_cast<std::uint32_t>(code), std::memory_order_relaxed);
				g_firstFaultPointer.store(reinterpret_cast<std::uintptr_t>(pointer), std::memory_order_relaxed);
				g_firstFaultModule.store(module, std::memory_order_relaxed);
			}
			if (ordinal < 8) {
				std::uintptr_t module = 0;
				const auto shape = InspectComObject(pointer, module);
				char line[240]{};
				std::snprintf(line, sizeof(line),
					"restore fault #%u kind=%s index=%u name=%s code=0x%08lx pointer=0x%llx shape=%u module=0x%llx",
					static_cast<unsigned>(ordinal + 1),
					kind == 0 ? "restore-step" : (kind == 1 ? "snapshot-release" : "shutdown-release"),
					static_cast<unsigned>(index),
					kind == 1 ? ToString(static_cast<SnapshotOrdinal>(index)) : "-",
					code,
					static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(pointer)),
					static_cast<unsigned>(shape),
					static_cast<unsigned long long>(module));
				Emit(line);
			}
			return EXCEPTION_EXECUTE_HANDLER;
		}

		void ReleaseInterfaceSEH(
			IUnknown*& value,
			bool& faulted,
			const ReleaseDomain domain,
			const std::uint32_t ordinal = 0) noexcept
		{
			auto* pending = value;
			value = nullptr;
			if (!pending)
				return;
#if !defined(RR_MIRROR_PANE_RENDERER_TEST_HOOKS)
			(void)domain;
			(void)ordinal;
#endif
#if defined(RR_MIRROR_PANE_RENDERER_TEST_HOOKS)
			auto& injection = g_cleanupFaultInjection;
			if (domain == ReleaseDomain::kSnapshot)
				++injection.telemetry.snapshotReleasesAttempted;
			else
				++injection.telemetry.shutdownReleasesAttempted;
#endif
			bool released = false;
			__try {
#if defined(RR_MIRROR_PANE_RENDERER_TEST_HOOKS)
				if (!injection.consumed && domain == ReleaseDomain::kShutdown &&
					ordinal == 0 && injection.site ==
						TestHooks::CleanupFaultSite::kShutdownFirstRelease) {
					injection.consumed = true;
					RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
				}
#endif
				pending->Release();
				released = true;
			} __except (RecordRestoreFault(
				domain == ReleaseDomain::kSnapshot ? 1 : 2,
				static_cast<std::uint8_t>(ordinal), pending,
				GetExceptionCode())) {
				faulted = true;
			}
#if defined(RR_MIRROR_PANE_RENDERER_TEST_HOOKS)
			if (released) {
				if (domain == ReleaseDomain::kSnapshot)
					++injection.telemetry.snapshotReleasesCompleted;
				else
					++injection.telemetry.shutdownReleasesCompleted;
			}
#endif
		}

		template <class T>
		void ReleaseInterfaceSEH(
			T*& value,
			bool& faulted,
			const ReleaseDomain domain = ReleaseDomain::kSnapshot,
			const std::uint32_t ordinal = 0) noexcept
		{
			IUnknown* unknown = value;
			value = nullptr;
			ReleaseInterfaceSEH(unknown, faulted, domain, ordinal);
		}

		template <class Shader>
		void ReleaseShaderStageSEH(
			ShaderStageSnapshot<Shader>& stage,
			bool& faulted,
			const SnapshotOrdinal shaderOrdinal,
			const SnapshotOrdinal instanceOrdinal) noexcept
		{
			// XSGetShader AddRefs and writes only the first *pNumClassInstances
			// entries; the rest of the array is not the runtime's to touch.  ENB's
			// wrapped context (2026-09-10, AE 1.7.104 + ENB 0.505) reports zero
			// instances yet leaves non-COM garbage in later slots; releasing every
			// slot access-violated on the vertex and pixel stages.  Only the
			// reported entries are owned, exactly as the D3D11 contract states.
			const std::size_t owned = (std::min)(
				static_cast<std::size_t>(stage.instanceCount), stage.instances.size());
			for (std::size_t index = 0; index < owned; ++index) {
				ReleaseInterfaceSEH(stage.instances[index], faulted,
					ReleaseDomain::kSnapshot, static_cast<std::uint32_t>(instanceOrdinal));
			}
			for (std::size_t index = owned; index < stage.instances.size(); ++index)
				stage.instances[index] = nullptr;
			ReleaseInterfaceSEH(stage.shader, faulted, ReleaseDomain::kSnapshot,
				static_cast<std::uint32_t>(shaderOrdinal));
			stage.instanceCount = 0;
		}

		void ReleaseRawContextStateSEH(
			RawContextStateSnapshot& state,
			bool& faulted) noexcept
		{
			// Clear every owned pointer before its independently guarded Release.
			// A bad COM vtable can leak one reference, but can neither preserve an
			// authorization nor prevent cleanup of any other captured object.
			constexpr auto kSnapshot = ReleaseDomain::kSnapshot;
			const auto ordinal = [](const SnapshotOrdinal value) noexcept {
				return static_cast<std::uint32_t>(value);
			};
			ReleaseInterfaceSEH(state.predicate, faulted, kSnapshot, ordinal(SnapshotOrdinal::kPredicate));
			ReleaseInterfaceSEH(state.depthStencilView, faulted, kSnapshot, ordinal(SnapshotOrdinal::kDepthStencilView));
			for (std::size_t index = 0; index < state.renderTargets.size(); ++index) {
				ReleaseInterfaceSEH(state.renderTargets[index], faulted, kSnapshot,
					ordinal(SnapshotOrdinal::kRenderTarget0) + static_cast<std::uint32_t>(index));
			}
			ReleaseInterfaceSEH(state.depthStencilState, faulted, kSnapshot, ordinal(SnapshotOrdinal::kDepthStencilState));
			ReleaseInterfaceSEH(state.blendState, faulted, kSnapshot, ordinal(SnapshotOrdinal::kBlendState));
			ReleaseInterfaceSEH(state.rasterizerState, faulted, kSnapshot, ordinal(SnapshotOrdinal::kRasterizerState));
			ReleaseInterfaceSEH(state.pixelSampler, faulted, kSnapshot, ordinal(SnapshotOrdinal::kPixelSampler));
			for (std::size_t index = 0; index < state.pixelShaderResources.size(); ++index) {
				ReleaseInterfaceSEH(state.pixelShaderResources[index], faulted, kSnapshot,
					ordinal(SnapshotOrdinal::kPixelResource0) + static_cast<std::uint32_t>(index));
			}
			ReleaseInterfaceSEH(state.pixelConstantBuffer, faulted, kSnapshot, ordinal(SnapshotOrdinal::kPixelConstantBuffer));
			ReleaseInterfaceSEH(state.vertexConstantBuffer, faulted, kSnapshot, ordinal(SnapshotOrdinal::kVertexConstantBuffer));
			ReleaseShaderStageSEH(state.domainStage, faulted, SnapshotOrdinal::kDomainShader, SnapshotOrdinal::kDomainInstances);
			ReleaseShaderStageSEH(state.hullStage, faulted, SnapshotOrdinal::kHullShader, SnapshotOrdinal::kHullInstances);
			ReleaseShaderStageSEH(state.geometryStage, faulted, SnapshotOrdinal::kGeometryShader, SnapshotOrdinal::kGeometryInstances);
			ReleaseShaderStageSEH(state.pixelStage, faulted, SnapshotOrdinal::kPixelShader, SnapshotOrdinal::kPixelInstances);
			ReleaseShaderStageSEH(state.vertexStage, faulted, SnapshotOrdinal::kVertexShader, SnapshotOrdinal::kVertexInstances);
			ReleaseInterfaceSEH(state.indexBuffer, faulted, kSnapshot, ordinal(SnapshotOrdinal::kIndexBuffer));
			ReleaseInterfaceSEH(state.vertexBuffer, faulted, kSnapshot, ordinal(SnapshotOrdinal::kVertexBuffer));
			ReleaseInterfaceSEH(state.inputLayout, faulted, kSnapshot, ordinal(SnapshotOrdinal::kInputLayout));
			state = {};
		}

		void ExecuteRestoreStepUnsafe(
			ID3D11DeviceContext* context,
			const RawContextStateSnapshot* state,
			const RestoreStep step) noexcept
		{
			switch (step) {
			case RestoreStep::kUnbindPrivateResources: {
				ID3D11ShaderResourceView* resources[2]{ nullptr, nullptr };
				context->PSSetShaderResources(0, 2, resources);
				break;
			}
			case RestoreStep::kInputLayout:
				context->IASetInputLayout(state->inputLayout);
				break;
			case RestoreStep::kVertexBuffer: {
				ID3D11Buffer* buffer = state->vertexBuffer;
				context->IASetVertexBuffers(
					0, 1, &buffer, &state->vertexStride, &state->vertexOffset);
				break;
			}
			case RestoreStep::kIndexBuffer:
				context->IASetIndexBuffer(
					state->indexBuffer, state->indexFormat, state->indexOffset);
				break;
			case RestoreStep::kTopology:
				context->IASetPrimitiveTopology(state->topology);
				break;
			case RestoreStep::kVertexShader:
				RestoreShaderStage(state->vertexStage,
					[context](auto shader, auto instances, UINT count) {
						context->VSSetShader(shader, instances, count);
					});
				break;
			case RestoreStep::kPixelShader:
				RestoreShaderStage(state->pixelStage,
					[context](auto shader, auto instances, UINT count) {
						context->PSSetShader(shader, instances, count);
					});
				break;
			case RestoreStep::kGeometryShader:
				RestoreShaderStage(state->geometryStage,
					[context](auto shader, auto instances, UINT count) {
						context->GSSetShader(shader, instances, count);
					});
				break;
			case RestoreStep::kHullShader:
				if (!state->tessellationCaptured)
					break;
				RestoreShaderStage(state->hullStage,
					[context](auto shader, auto instances, UINT count) {
						context->HSSetShader(shader, instances, count);
					});
				break;
			case RestoreStep::kDomainShader:
				if (!state->tessellationCaptured)
					break;
				RestoreShaderStage(state->domainStage,
					[context](auto shader, auto instances, UINT count) {
						context->DSSetShader(shader, instances, count);
					});
				break;
			case RestoreStep::kVertexConstantBuffer: {
				ID3D11Buffer* buffer = state->vertexConstantBuffer;
				context->VSSetConstantBuffers(0, 1, &buffer);
				break;
			}
			case RestoreStep::kPixelConstantBuffer: {
				ID3D11Buffer* buffer = state->pixelConstantBuffer;
				context->PSSetConstantBuffers(0, 1, &buffer);
				break;
			}
			case RestoreStep::kPixelSampler: {
				ID3D11SamplerState* sampler = state->pixelSampler;
				context->PSSetSamplers(0, 1, &sampler);
				break;
			}
			case RestoreStep::kRasterizerState:
				context->RSSetState(state->rasterizerState);
				break;
			case RestoreStep::kViewports:
				context->RSSetViewports(
					state->viewportCount,
					state->viewportCount != 0 ? state->viewports.data() : nullptr);
				break;
			case RestoreStep::kBlendState:
				context->OMSetBlendState(
					state->blendState, state->blendFactor.data(), state->sampleMask);
				break;
			case RestoreStep::kDepthStencilState:
				context->OMSetDepthStencilState(
					state->depthStencilState, state->stencilReference);
				break;
			case RestoreStep::kRenderTargets:
				context->OMSetRenderTargets(
					static_cast<UINT>(state->renderTargets.size()),
					state->renderTargets.data(), state->depthStencilView);
				break;
			case RestoreStep::kPixelResources: {
				// Restore t0/t1 only after the custom RTVs have been removed.
				context->PSSetShaderResources(
					0, static_cast<UINT>(state->pixelShaderResources.size()),
					state->pixelShaderResources.data());
				break;
			}
			case RestoreStep::kPredication:
				context->SetPredication(state->predicate, state->predicateValue);
				break;
			case RestoreStep::kCount:
			default:
				break;
			}
		}

		__declspec(noinline) void ExecuteRestoreStepSEH(
			ID3D11DeviceContext* context,
			const RawContextStateSnapshot* state,
			const RestoreStep step,
			bool& faulted) noexcept
		{
#if defined(RR_MIRROR_PANE_RENDERER_TEST_HOOKS)
			auto& injection = g_cleanupFaultInjection;
			++injection.telemetry.restoreStepsAttempted;
#endif
			bool completed = false;
			__try {
#if defined(RR_MIRROR_PANE_RENDERER_TEST_HOOKS)
				if (!injection.consumed && step == RestoreStep::kInputLayout &&
					injection.site ==
						TestHooks::CleanupFaultSite::kRestoreInputLayout) {
					injection.consumed = true;
					RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
				}
#endif
				ExecuteRestoreStepUnsafe(context, state, step);
				completed = true;
			} __except (RecordRestoreFault(
				0, static_cast<std::uint8_t>(step), nullptr, GetExceptionCode())) {
				faulted = true;
			}
#if defined(RR_MIRROR_PANE_RENDERER_TEST_HOOKS)
			if (completed)
				++injection.telemetry.restoreStepsCompleted;
#endif
		}

		class ContextStateSnapshot
		{
		public:
			explicit ContextStateSnapshot(ID3D11DeviceContext* context) noexcept
			{
				captureSucceeded = context && CaptureContextStateSEH(context, &state);
				active = captureSucceeded;
				if (!captureSucceeded) {
					bool releaseFaulted = false;
					ReleaseRawContextStateSEH(state, releaseFaulted);
				}
			}

			~ContextStateSnapshot()
			{
				(void)Restore();
			}

			ContextStateSnapshot(const ContextStateSnapshot&) = delete;
			ContextStateSnapshot& operator=(const ContextStateSnapshot&) = delete;

			[[nodiscard]] bool Captured() const noexcept
			{
				return captureSucceeded;
			}

			[[nodiscard]] bool Restore() noexcept
			{
				if (!active)
					return restoreSucceeded;
				active = false;
				RawContextStateSnapshot detached = state;
				state = {};
				bool faulted = false;
				for (std::uint32_t value = 0;
					value < static_cast<std::uint32_t>(RestoreStep::kCount); ++value) {
					ExecuteRestoreStepSEH(
						detached.context, &detached,
						static_cast<RestoreStep>(value), faulted);
				}
				ReleaseRawContextStateSEH(detached, faulted);
				restoreSucceeded = !faulted;
				return restoreSucceeded;
			}

		private:
			RawContextStateSnapshot state{};
			bool captureSucceeded{ false };
			bool restoreSucceeded{ false };
			bool active{ false };
		};

		struct DrawBindings
		{
			ID3D11InputLayout* inputLayout{ nullptr };
			ID3D11Buffer* vertexBuffer{ nullptr };
			ID3D11Buffer* indexBuffer{ nullptr };
			UINT indexCount{ 0 };
			ID3D11VertexShader* vertexShader{ nullptr };
			ID3D11PixelShader* pixelShader{ nullptr };
			ID3D11Buffer* constantBuffer{ nullptr };
			ID3D11ShaderResourceView* reflectionSRV{ nullptr };
			ID3D11ShaderResourceView* reflectionDepthSRV{ nullptr };
			ID3D11SamplerState* sampler{ nullptr };
			ID3D11RasterizerState* rasterizer{ nullptr };
			ID3D11BlendState* blend{ nullptr };
			ID3D11DepthStencilState* depthStencil{ nullptr };
			ID3D11RenderTargetView* colorRTV{ nullptr };
			ID3D11RenderTargetView* motionRTV{ nullptr };
			ID3D11DepthStencilView* depthDSV{ nullptr };
			const D3D11_VIEWPORT* viewport{ nullptr };
			bool touchTessellationStages{ true };
		};

		__declspec(noinline) void CopyMappedConstantsWithSEHUnmap(
			ID3D11DeviceContext* a_context,
			ID3D11Buffer* a_buffer,
			void* a_destination,
			const void* a_source,
			std::size_t a_size) noexcept
		{
			__try {
				std::memcpy(a_destination, a_source, a_size);
			} __finally {
				a_context->Unmap(a_buffer, 0);
			}
		}

		__declspec(noinline) void IssuePaneDraw(
			ID3D11DeviceContext* a_context,
			const DrawBindings* a_bindings) noexcept
		{
			a_context->SetPredication(nullptr, FALSE);

			a_context->IASetInputLayout(a_bindings->inputLayout);
			constexpr UINT vertexStride = sizeof(Vertex);
			constexpr UINT vertexOffset = 0;
			a_context->IASetVertexBuffers(
				0, 1, &a_bindings->vertexBuffer, &vertexStride, &vertexOffset);
			a_context->IASetIndexBuffer(
				a_bindings->indexBuffer, DXGI_FORMAT_R16_UINT, 0);
			a_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			a_context->VSSetShader(a_bindings->vertexShader, nullptr, 0);
			a_context->PSSetShader(a_bindings->pixelShader, nullptr, 0);
			a_context->GSSetShader(nullptr, nullptr, 0);
			if (a_bindings->touchTessellationStages) {
				a_context->HSSetShader(nullptr, nullptr, 0);
				a_context->DSSetShader(nullptr, nullptr, 0);
			}
			a_context->VSSetConstantBuffers(0, 1, &a_bindings->constantBuffer);
			a_context->PSSetConstantBuffers(0, 1, &a_bindings->constantBuffer);
			ID3D11ShaderResourceView* resources[2]{
				a_bindings->reflectionSRV, a_bindings->reflectionDepthSRV };
			a_context->PSSetShaderResources(0, 2, resources);
			a_context->PSSetSamplers(0, 1, &a_bindings->sampler);

			a_context->RSSetState(a_bindings->rasterizer);
			a_context->RSSetViewports(1, a_bindings->viewport);
			a_context->OMSetBlendState(a_bindings->blend, nullptr, 0xFFFFFFFFu);
			a_context->OMSetDepthStencilState(a_bindings->depthStencil, 0);
			ID3D11RenderTargetView* targets[2]{
				a_bindings->colorRTV, a_bindings->motionRTV };
			a_context->OMSetRenderTargets(
				a_bindings->motionRTV ? 2u : 1u, targets, a_bindings->depthDSV);
			if (a_bindings->indexBuffer)
				a_context->DrawIndexed(a_bindings->indexCount, 0, 0);
			else
				a_context->Draw(a_bindings->indexCount, 0);
		}

		// This function deliberately contains no C++ objects which require unwind.
		// /EHsc does not run C++ destructors for an access violation, but an SEH
		// __finally block still runs.  The outer M5 SEH boundary may therefore
		// fault-latch delivery without returning to Skyrim with our D3D state bound.
		__declspec(noinline) void IssuePaneDrawWithSEHRestore(
			ID3D11DeviceContext* a_context,
			const DrawBindings* a_bindings,
			ContextStateSnapshot* a_savedState,
			bool* a_restoreSucceeded) noexcept
		{
			__try {
				IssuePaneDraw(a_context, a_bindings);
			} __finally {
				*a_restoreSucceeded = a_savedState->Restore();
			}
		}

		bool IssueDepthProbeGuarded(ID3D11DeviceContext* context,
			DepthProbeOperation operation, void* opaque, ContextStateSnapshot* saved,
			bool* restored) noexcept
		{
			bool completed = false;
			__try {
				__try { completed = operation(context, TouchTessellationStages(), opaque); }
				__finally { *restored = saved->Restore(); }
			} __except (EXCEPTION_EXECUTE_HANDLER) { completed = false; }
			return completed;
		}
	}

	DrawStatus RunDepthProbeWithPreservedState(ID3D11DeviceContext* context,
		DepthProbeOperation operation, void* opaque) noexcept
	{
		if (!context || !operation) return DrawStatus::kInvalidArgument;
		bool faulted = false;
		if (AnyStreamOutputTargetBound(context, faulted)) return DrawStatus::kStreamOutputBound;
		if (faulted) return DrawStatus::kContextStateCaptureFailed;
		if (D3D11OutputMergerState::QueryUAVState(context) != D3D11OutputMergerState::UAVState::kNone)
			return DrawStatus::kOutputMergerUAVUnsafe;
		ContextStateSnapshot saved(context);
		if (!saved.Captured()) return DrawStatus::kContextStateCaptureFailed;
		bool restored = false;
		const bool completed = IssueDepthProbeGuarded(context, operation, opaque, &saved, &restored);
		return !restored ? DrawStatus::kContextStateRestoreFailed :
			completed ? DrawStatus::kDrawn : DrawStatus::kContextStateCaptureFailed;
	}

#if defined(RR_MIRROR_PANE_RENDERER_TEST_HOOKS)
	namespace TestHooks
	{
		void ResetCleanupFaultInjection() noexcept
		{
			g_cleanupFaultInjection = {};
		}

		void SetCleanupFaultSite(const CleanupFaultSite site) noexcept
		{
			g_cleanupFaultInjection.site = site;
			g_cleanupFaultInjection.consumed = false;
		}

		CleanupTelemetry GetCleanupTelemetry() noexcept
		{
			return g_cleanupFaultInjection.telemetry;
		}
	}
#endif

	RestoreFaultTelemetry GetRestoreFaultTelemetry() noexcept
	{
		return {
			.faults = g_restoreFaults.load(std::memory_order_relaxed),
			.lastExceptionCode = g_restoreFaultCode.load(std::memory_order_relaxed),
			.lastStep = g_restoreFaultStep.load(std::memory_order_relaxed),
			.firstKind = g_firstFaultKind.load(std::memory_order_relaxed),
			.firstIndex = g_firstFaultIndex.load(std::memory_order_relaxed),
			.firstExceptionCode = g_firstFaultCode.load(std::memory_order_relaxed),
			.firstPointer = g_firstFaultPointer.load(std::memory_order_relaxed),
			.firstPointerModule = g_firstFaultModule.load(std::memory_order_relaxed),
			.releaseOrdinalMask = g_releaseOrdinalMask.load(std::memory_order_relaxed),
			.restoreStepMask = g_restoreStepMask.load(std::memory_order_relaxed)
		};
	}

	const char* ToString(InitializationStatus status) noexcept
	{
		switch (status) {
		case InitializationStatus::kReady:
			return "ready";
		case InitializationStatus::kInvalidDevice:
			return "invalid-device";
		case InitializationStatus::kUnsupportedFeatureLevel:
			return "unsupported-feature-level";
		case InitializationStatus::kVertexShaderCreationFailed:
			return "vertex-shader-creation-failed";
		case InitializationStatus::kPixelShaderCreationFailed:
			return "pixel-shader-creation-failed";
		case InitializationStatus::kInputLayoutCreationFailed:
			return "input-layout-creation-failed";
		case InitializationStatus::kVertexBufferCreationFailed:
			return "vertex-buffer-creation-failed";
		case InitializationStatus::kIndexBufferCreationFailed:
			return "index-buffer-creation-failed";
		case InitializationStatus::kConstantBufferCreationFailed:
			return "constant-buffer-creation-failed";
		case InitializationStatus::kSamplerCreationFailed:
			return "sampler-creation-failed";
		case InitializationStatus::kBlendCreationFailed:
			return "blend-creation-failed";
		case InitializationStatus::kRasterizerCreationFailed:
			return "rasterizer-creation-failed";
		case InitializationStatus::kDepthStencilCreationFailed:
			return "depth-stencil-creation-failed";
		default:
			return "unknown";
		}
	}

	bool TakeArrayColorTargetRejectionNotice() noexcept
	{
		// Returns true exactly once, after the first multi-slice colour view has
		// been refused, so the integration layer (which owns the log sink) can
		// report the condition a single time.
		if (g_arrayColorTargetRejects.load(std::memory_order_relaxed) == 0)
			return false;
		return !g_arrayColorTargetRejectNoticeTaken.exchange(
			true, std::memory_order_acq_rel);
	}

	const char* ToString(DrawStatus a_status) noexcept
	{
		switch (a_status) {
		case DrawStatus::kDrawn:
			return "drawn";
		case DrawStatus::kDrawnPartialCoverage:
			return "drawn-partial-coverage";
		case DrawStatus::kNotInitialized:
			return "not-initialized";
		case DrawStatus::kInvalidArgument:
			return "invalid-argument";
		case DrawStatus::kNoPublishedFrame:
			return "no-published-frame";
		case DrawStatus::kCandidateMismatch:
			return "candidate-mismatch";
		case DrawStatus::kDeviceMismatch:
			return "device-mismatch";
		case DrawStatus::kProjectionRejected:
			return "projection-rejected";
		case DrawStatus::kProjectionMainViewNotVisible:
			return "projection-main-view-not-visible";
		case DrawStatus::kProjectionReflectedUncovered:
			return "projection-reflected-uncovered";
		case DrawStatus::kColorTargetMissing:
			return "color-target-missing";
		case DrawStatus::kDepthTargetMissing:
			return "depth-target-missing";
		case DrawStatus::kViewportRejected:
			return "viewport-rejected";
		case DrawStatus::kTargetDeviceMismatch:
			return "target-device-mismatch";
		case DrawStatus::kDepthTargetReadOnly:
			return "depth-target-read-only";
		case DrawStatus::kTargetResourceRejected:
			return "target-resource-rejected";
		case DrawStatus::kTargetAliasRejected:
			return "target-alias-rejected";
		case DrawStatus::kTargetShapeRejected:
			return "target-shape-rejected";
		case DrawStatus::kTargetBindFlagsRejected:
			return "target-bind-flags-rejected";
		case DrawStatus::kShaderResourceAliasBound:
			return "shader-resource-alias-bound";
		case DrawStatus::kComputeUAVAliasBound:
			return "compute-uav-alias-bound";
		case DrawStatus::kOutputMergerUAVUnsafe:
			return "output-merger-uav-unsafe";
		case DrawStatus::kStreamOutputBound:
			return "stream-output-bound";
		case DrawStatus::kConstantBufferMapFailed:
			return "constant-buffer-map-failed";
		case DrawStatus::kCustomVertexBufferCreationFailed:
			return "custom-vertex-buffer-creation-failed";
		case DrawStatus::kCustomVertexBufferMapFailed:
			return "custom-vertex-buffer-map-failed";
		case DrawStatus::kContextStateCaptureFailed:
			return "context-state-capture-failed";
		case DrawStatus::kContextStateRestoreFailed:
			return "context-state-restore-failed";
		default:
			return "unknown";
		}
	}

	DrawStatus ClassifyProjectionCoverage(
		const PaneTransform& pane,
		const MainView& mainView,
		const PublishedFrame& frame,
		std::span<const DirectX::XMFLOAT2> customPaneTriangles) noexcept
	{
		if (!customPaneTriangles.empty()) {
			if (ResolvePaneApertureKind(frame) != PaneApertureKind::kWallQuad ||
				!MirrorNifContract::ValidTriangles(customPaneTriangles)) return DrawStatus::kInvalidArgument;
			bool visible = false, reflected = false, uncovered = false;
			for (std::size_t i = 0; i < customPaneTriangles.size(); i += 3) {
				const auto coverage = EvaluateProjectionCoverage(pane,mainView,frame,customPaneTriangles.subspan(i,3));
				if (coverage == ProjectionCoverage::kRejected) return DrawStatus::kProjectionRejected;
				visible |= coverage != ProjectionCoverage::kMainViewNotVisible;
				reflected |= coverage == ProjectionCoverage::kPartial || coverage == ProjectionCoverage::kFull;
				uncovered |= coverage == ProjectionCoverage::kReflectedUncovered || coverage == ProjectionCoverage::kPartial;
			}
			if (!visible) return DrawStatus::kProjectionMainViewNotVisible;
			if (!reflected) return DrawStatus::kProjectionReflectedUncovered;
			return uncovered ? DrawStatus::kDrawnPartialCoverage : DrawStatus::kDrawn;
		}
		switch (EvaluateProjectionCoverage(pane, mainView, frame)) {
		case ProjectionCoverage::kFull:
			return DrawStatus::kDrawn;
		case ProjectionCoverage::kPartial:
			return DrawStatus::kDrawnPartialCoverage;
		case ProjectionCoverage::kMainViewNotVisible:
			return DrawStatus::kProjectionMainViewNotVisible;
		case ProjectionCoverage::kReflectedUncovered:
			return DrawStatus::kProjectionReflectedUncovered;
		case ProjectionCoverage::kRejected:
		default:
			return DrawStatus::kProjectionRejected;
		}
	}

	bool HasFullHandReflectedApertureCoverage(
		const PaneTransform& pane,
		const PublishedFrame& frame) noexcept
	{
		if (!frame.valid ||
			frame.owner.kind !=
				HandMirrorRuntimeBridgePolicy::OwnerKind::kHand ||
			frame.audience != HandMirrorRuntimeBridgePolicy::
				PublicationAudience::kInternalHandOnly ||
			!HandMirrorRuntimeBridgePolicy::SameOwner(frame.owner, pane.owner) ||
			!ValidPane(pane) || !Finite(frame.reflectedOrigin) ||
			!Finite(frame.reflectedViewProjection)) {
			return false;
		}

		const Vertex* vertices = nullptr;
		std::size_t vertexCount = 0;
		switch (ResolvePaneApertureKind(frame)) {
		case PaneApertureKind::kHandRound16:
			vertices = kHandRound16Vertices.data() + 1;
			vertexCount = kHandRound16Vertices.size() - 1;
			break;
		case PaneApertureKind::kHandFiligree24:
			vertices = kHandFiligree24Vertices.data() + 1;
			vertexCount = kHandFiligree24Vertices.size() - 1;
			break;
		case PaneApertureKind::kWallQuad:
		default:
			return false;
		}

		// The aperture and reflected clip volume are convex.  Every boundary
		// vertex inside all five shader-owned homogeneous half-spaces proves the
		// complete aperture is safe, independent of which portion a main view sees.
		for (std::size_t index = 0; index < vertexCount; ++index) {
			const auto world = WorldCorner(
				pane, vertices[index].unitPosition.x,
				vertices[index].unitPosition.y);
			const auto clip = ProjectRelative(
				world, frame.reflectedOrigin,
				frame.reflectedViewProjection);
			if (!Finite(clip) || !(clip.w > kProjectionEpsilon) ||
				clip.x < -clip.w || clip.x > clip.w ||
				clip.y < -clip.w || clip.y > clip.w) {
				return false;
			}
		}
		return vertexCount != 0;
	}

	HandCoverProjectionTelemetry InspectHandCoverProjection(
		const PaneTransform& pane,
		const MainView& mainView,
		const PublishedFrame& frame) noexcept
	{
		HandCoverProjectionTelemetry telemetry{};
		telemetry.handAperture =
			ResolvePaneApertureKind(frame) != PaneApertureKind::kWallQuad;
		if (!telemetry.handAperture)
			return telemetry;
		telemetry.coverVertexCount = static_cast<std::uint8_t>(
			kHandCoverVertices.size());

		telemetry.finite = Finite(pane.center) && Finite(pane.tangentExtent) &&
			Finite(pane.bitangentExtent) && Finite(mainView.origin) &&
			Finite(mainView.viewProjection) && Finite(frame.reflectedOrigin) &&
			Finite(frame.reflectedViewProjection);
		if (!telemetry.finite || !ValidPane(pane))
			return telemetry;

		float minimumMainW = (std::numeric_limits<float>::max)();
		float minimumReflectedW = (std::numeric_limits<float>::max)();
		for (const auto& vertex : kHandCoverVertices) {
			const auto world = WorldCorner(
				pane, vertex.unitPosition.x, vertex.unitPosition.y);
			const auto mainClip = ProjectRelative(
				world, mainView.origin, mainView.viewProjection);
			const auto reflectedClip = ProjectRelative(
				world, frame.reflectedOrigin, frame.reflectedViewProjection);
			if (!Finite(mainClip) || !Finite(reflectedClip)) {
				telemetry.finite = false;
				return telemetry;
			}
			minimumMainW = (std::min)(minimumMainW, mainClip.w);
			minimumReflectedW =
				(std::min)(minimumReflectedW, reflectedClip.w);
			if (mainClip.w > kProjectionEpsilon)
				++telemetry.mainPositiveWVertices;
			if (reflectedClip.w > kProjectionEpsilon)
				++telemetry.reflectedPositiveWVertices;
		}

		telemetry.minimumMainClipW = minimumMainW;
		telemetry.minimumReflectedClipW = minimumReflectedW;
		telemetry.valid = true;
		return telemetry;
	}

	inline constexpr float kCoplanarBiasClampNear = -2.0e-4f;
	inline constexpr float kCoplanarBiasWorldUnits = 0.5f;
	// fNearDistance's default. The result is clamped at both ends, so an install
	// that moved it shifts where the curve saturates, never past the two bounds.
	inline constexpr float kAssumedNearPlane = 15.0f;

	bool Renderer::Initialize(
		ID3D11Device* a_device,
		InitializationStatus* status,
		SamplingMode samplingMode,
		bool replaceCoplanarFallback) noexcept
	{
		if (status)
			*status = InitializationStatus::kInvalidDevice;
		if (!a_device)
			return false;
		if (a_device->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0) {
			if (status)
				*status = InitializationStatus::kUnsupportedFeatureLevel;
			return false;
		}
		if (Ready() && device.Get() == a_device &&
			activeSamplingMode == samplingMode &&
			coplanarFallbackReplacement == replaceCoplanarFallback) {
			if (status)
				*status = InitializationStatus::kReady;
			return true;
		}

		Microsoft::WRL::ComPtr<ID3D11VertexShader> newVertexShader;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> newPixelShader;
		Microsoft::WRL::ComPtr<ID3D11InputLayout> newInputLayout;
		Microsoft::WRL::ComPtr<ID3D11Buffer> newVertexBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer> newIndexBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer> newHandVertexBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer> newHandIndexBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer> newFiligreeHandVertexBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer> newFiligreeHandIndexBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer> newConstantBuffer;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> newSamplerState;
		Microsoft::WRL::ComPtr<ID3D11BlendState> newBlendState;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> newRasterizerState;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> newDepthStencilState;

		if (FAILED(a_device->CreateVertexShader(
				kMirrorPaneVertexShader, sizeof(kMirrorPaneVertexShader), nullptr,
				newVertexShader.GetAddressOf()))) {
			if (status)
				*status = InitializationStatus::kVertexShaderCreationFailed;
			return false;
		}
		if (FAILED(a_device->CreatePixelShader(
				kMirrorPanePixelShader, sizeof(kMirrorPanePixelShader), nullptr,
				newPixelShader.GetAddressOf()))) {
			if (status)
				*status = InitializationStatus::kPixelShaderCreationFailed;
			return false;
		}

		const D3D11_INPUT_ELEMENT_DESC inputElement{
			"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
			D3D11_INPUT_PER_VERTEX_DATA, 0
		};
		if (FAILED(a_device->CreateInputLayout(
				&inputElement, 1, kMirrorPaneVertexShader,
				sizeof(kMirrorPaneVertexShader), newInputLayout.GetAddressOf()))) {
			if (status)
				*status = InitializationStatus::kInputLayoutCreationFailed;
			return false;
		}

		D3D11_BUFFER_DESC vertexDescription{};
		vertexDescription.ByteWidth = static_cast<UINT>(sizeof(kVertices));
		vertexDescription.Usage = D3D11_USAGE_IMMUTABLE;
		vertexDescription.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		const D3D11_SUBRESOURCE_DATA vertexData{ kVertices.data(), 0, 0 };
		if (FAILED(a_device->CreateBuffer(
				&vertexDescription, &vertexData, newVertexBuffer.GetAddressOf()))) {
			if (status)
				*status = InitializationStatus::kVertexBufferCreationFailed;
			return false;
		}

		D3D11_BUFFER_DESC indexDescription{};
		indexDescription.ByteWidth = static_cast<UINT>(sizeof(kIndices));
		indexDescription.Usage = D3D11_USAGE_IMMUTABLE;
		indexDescription.BindFlags = D3D11_BIND_INDEX_BUFFER;
		const D3D11_SUBRESOURCE_DATA indexData{ kIndices.data(), 0, 0 };
		if (FAILED(a_device->CreateBuffer(
				&indexDescription, &indexData, newIndexBuffer.GetAddressOf()))) {
			if (status)
				*status = InitializationStatus::kIndexBufferCreationFailed;
			return false;
		}

		D3D11_BUFFER_DESC handVertexDescription{};
		handVertexDescription.ByteWidth =
			static_cast<UINT>(sizeof(kHandCoverVertices));
		handVertexDescription.Usage = D3D11_USAGE_IMMUTABLE;
		handVertexDescription.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		const D3D11_SUBRESOURCE_DATA handVertexData{
			kHandCoverVertices.data(), 0, 0
		};
		if (FAILED(a_device->CreateBuffer(
				&handVertexDescription, &handVertexData,
				newHandVertexBuffer.GetAddressOf()))) {
			if (status)
				*status = InitializationStatus::kVertexBufferCreationFailed;
			return false;
		}

		D3D11_BUFFER_DESC handIndexDescription{};
		handIndexDescription.ByteWidth =
			static_cast<UINT>(sizeof(kHandCoverIndices));
		handIndexDescription.Usage = D3D11_USAGE_IMMUTABLE;
		handIndexDescription.BindFlags = D3D11_BIND_INDEX_BUFFER;
		const D3D11_SUBRESOURCE_DATA handIndexData{
			kHandCoverIndices.data(), 0, 0
		};
		if (FAILED(a_device->CreateBuffer(
				&handIndexDescription, &handIndexData,
				newHandIndexBuffer.GetAddressOf()))) {
			if (status)
				*status = InitializationStatus::kIndexBufferCreationFailed;
			return false;
		}

		D3D11_BUFFER_DESC filigreeHandVertexDescription{};
		filigreeHandVertexDescription.ByteWidth =
			static_cast<UINT>(sizeof(kHandCoverVertices));
		filigreeHandVertexDescription.Usage = D3D11_USAGE_IMMUTABLE;
		filigreeHandVertexDescription.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		const D3D11_SUBRESOURCE_DATA filigreeHandVertexData{
			kHandCoverVertices.data(), 0, 0
		};
		if (FAILED(a_device->CreateBuffer(
				&filigreeHandVertexDescription, &filigreeHandVertexData,
				newFiligreeHandVertexBuffer.GetAddressOf()))) {
			if (status)
				*status = InitializationStatus::kVertexBufferCreationFailed;
			return false;
		}

		D3D11_BUFFER_DESC filigreeHandIndexDescription{};
		filigreeHandIndexDescription.ByteWidth =
			static_cast<UINT>(sizeof(kHandCoverIndices));
		filigreeHandIndexDescription.Usage = D3D11_USAGE_IMMUTABLE;
		filigreeHandIndexDescription.BindFlags = D3D11_BIND_INDEX_BUFFER;
		const D3D11_SUBRESOURCE_DATA filigreeHandIndexData{
			kHandCoverIndices.data(), 0, 0
		};
		if (FAILED(a_device->CreateBuffer(
				&filigreeHandIndexDescription, &filigreeHandIndexData,
				newFiligreeHandIndexBuffer.GetAddressOf()))) {
			if (status)
				*status = InitializationStatus::kIndexBufferCreationFailed;
			return false;
		}

		D3D11_BUFFER_DESC constantDescription{};
		constantDescription.ByteWidth = static_cast<UINT>(sizeof(PaneConstants));
		constantDescription.Usage = D3D11_USAGE_DYNAMIC;
		constantDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		constantDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(a_device->CreateBuffer(
				&constantDescription, nullptr, newConstantBuffer.GetAddressOf()))) {
			if (status)
				*status = InitializationStatus::kConstantBufferCreationFailed;
			return false;
		}

		D3D11_SAMPLER_DESC samplerDescription{};
		// Anisotropic + mips: the pane is viewed at oblique angles under heavy
		// minification; trilinear alone over-blurs and LOD-0 shimmered.  Keep the
		// hardware-selected LOD unbiased for ordinary panes. The dedicated hand
		// renderer uses a slight positive bias over the same complete chain.  The
		// former hand ceiling at mip 1 forced every footprint coarser than 2:1
		// (oblique panes, the 2048 capture over a ~600 pixel aperture) to
		// undersample an unantialiased capture, which is exactly the angle-driven
		// stair/saw edge on thin high-contrast silhouettes.  The 2x2 spatial
		// resolve is unchanged; the true footprint LOD is what removes the crawl.
		const bool isotropicHand =
			samplingMode == SamplingMode::kStableHandIsotropic;
		samplerDescription.Filter = isotropicHand ?
			D3D11_FILTER_MIN_MAG_MIP_LINEAR : D3D11_FILTER_ANISOTROPIC;
		samplerDescription.MaxAnisotropy = isotropicHand ? 1 : 16;
		samplerDescription.MipLODBias =
			samplingMode == SamplingMode::kStableHandFullMipChain || isotropicHand ?
				0.25f : 0.0f;
		samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDescription.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
		samplerDescription.MinLOD = 0.0f;
		samplerDescription.MaxLOD =
			samplingMode == SamplingMode::kBaseMipOnly ?
				0.0f : D3D11_FLOAT32_MAX;
		if (FAILED(a_device->CreateSamplerState(
				&samplerDescription, newSamplerState.GetAddressOf()))) {
			if (status)
				*status = InitializationStatus::kSamplerCreationFailed;
			return false;
		}

		D3D11_BLEND_DESC blendDescription{};
		blendDescription.AlphaToCoverageEnable = FALSE;
		blendDescription.IndependentBlendEnable = TRUE;
		blendDescription.RenderTarget[0].BlendEnable = FALSE;
		blendDescription.RenderTarget[0].RenderTargetWriteMask =
			D3D11_COLOR_WRITE_ENABLE_ALL;
		auto& motionBlend = blendDescription.RenderTarget[1];
		motionBlend.BlendEnable = TRUE;
		motionBlend.SrcBlend = D3D11_BLEND_SRC_ALPHA;
		motionBlend.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		motionBlend.BlendOp = D3D11_BLEND_OP_ADD;
		motionBlend.SrcBlendAlpha = D3D11_BLEND_ONE;
		motionBlend.DestBlendAlpha = D3D11_BLEND_ZERO;
		motionBlend.BlendOpAlpha = D3D11_BLEND_OP_ADD;
		motionBlend.RenderTargetWriteMask =
			D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN;
		if (FAILED(a_device->CreateBlendState(
				&blendDescription, newBlendState.GetAddressOf()))) {
			if (status)
				*status = InitializationStatus::kBlendCreationFailed;
			return false;
		}

		D3D11_RASTERIZER_DESC rasterizerDescription{};
		rasterizerDescription.FillMode = D3D11_FILL_SOLID;
		rasterizerDescription.CullMode = D3D11_CULL_NONE;
		rasterizerDescription.FrontCounterClockwise = FALSE;
		// Native object-WVP and our world-relative pane arithmetic can differ by
		// a few depth ULPs even for the same plane. LESS_EQUAL alone then lets the
		// black authored fallback cover a camera-dependent part of the reflection
		// (Sleeping Giant Inn, screenshot 187). The opt-in admits that rounding
		// difference without moving the optical plane or changing its projection.
		// Close, steep views also magnify independently rounded screen positions:
		// the Markarth replay still loses 37% with constant bias alone. Allow two
		// rasterizer subpixel steps of depth slope, not a whole pixel. Foreground
		// depth testing remains enabled; the default and hand paths stay unbiased.
		rasterizerDescription.DepthBias = replaceCoplanarFallback ? -4 : 0;
		// Owner video 2026-09-14 (Whiterun pointed arch, close grazing view): the
		// slope term grew past the frame's front relief and a strip of the pane
		// quad under the post's inner edge won the depth test on some frames
		// (flickering line on the post). Bound the total bias toward the eye so
		// it can never exceed a few world units at any distance the owner stands
		// at (2e-4 NDC is about 4 units at 550 units with the 15-unit near plane,
		// far less up close); coplanar rounding needs only a few ULPs.
		// Distance-aware: see EnsureCoplanarBias. Initialize builds the close-range
		// state, which is what shipped, and the first Draw refines it for the
		// distance the pane is actually being seen at.
		rasterizerDescription.DepthBiasClamp =
			replaceCoplanarFallback ? kCoplanarBiasClampNear : 0.0f;
		rasterizerDescription.SlopeScaledDepthBias = replaceCoplanarFallback ?
			-2.0f / static_cast<float>(1u << D3D11_SUBPIXEL_FRACTIONAL_BIT_COUNT) : 0.0f;
		rasterizerDescription.DepthClipEnable = TRUE;
		rasterizerDescription.ScissorEnable = FALSE;
		rasterizerDescription.MultisampleEnable = FALSE;
		rasterizerDescription.AntialiasedLineEnable = FALSE;
		if (FAILED(a_device->CreateRasterizerState(
				&rasterizerDescription, newRasterizerState.GetAddressOf()))) {
			if (status)
				*status = InitializationStatus::kRasterizerCreationFailed;
			return false;
		}

		D3D11_DEPTH_STENCIL_DESC depthDescription{};
		depthDescription.DepthEnable = TRUE;
		depthDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		depthDescription.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
		depthDescription.StencilEnable = FALSE;
		depthDescription.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
		depthDescription.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
		if (FAILED(a_device->CreateDepthStencilState(
				&depthDescription, newDepthStencilState.GetAddressOf()))) {
			if (status)
				*status = InitializationStatus::kDepthStencilCreationFailed;
			return false;
		}

		Shutdown();
		device = a_device;
		inputLayout = std::move(newInputLayout);
		vertexShader = std::move(newVertexShader);
		pixelShader = std::move(newPixelShader);
		vertexBuffer = std::move(newVertexBuffer);
		indexBuffer = std::move(newIndexBuffer);
		handVertexBuffer = std::move(newHandVertexBuffer);
		handIndexBuffer = std::move(newHandIndexBuffer);
		filigreeHandVertexBuffer = std::move(newFiligreeHandVertexBuffer);
		filigreeHandIndexBuffer = std::move(newFiligreeHandIndexBuffer);
		constantBuffer = std::move(newConstantBuffer);
		samplerState = std::move(newSamplerState);
		blendState = std::move(newBlendState);
		rasterizerState = std::move(newRasterizerState);
		depthStencilState = std::move(newDepthStencilState);
		activeSamplingMode = samplingMode;
		coplanarFallbackReplacement = replaceCoplanarFallback;

		SetDebugName(inputLayout.Get(), "RealisticReflections.M5.Pane.InputLayout");
		SetDebugName(vertexShader.Get(), "RealisticReflections.M5.Pane.VertexShader");
		SetDebugName(pixelShader.Get(), "RealisticReflections.M5.Pane.PixelShader");
		SetDebugName(vertexBuffer.Get(), "RealisticReflections.M5.Pane.VertexBuffer");
		SetDebugName(indexBuffer.Get(), "RealisticReflections.M5.Pane.IndexBuffer");
		SetDebugName(
			handVertexBuffer.Get(),
			"RealisticReflections.HandMirror.RoundMaskCover.VertexBuffer");
		SetDebugName(
			handIndexBuffer.Get(),
			"RealisticReflections.HandMirror.RoundMaskCover.IndexBuffer");
		SetDebugName(
			filigreeHandVertexBuffer.Get(),
			"RealisticReflections.HandMirror.FiligreeMaskCover.VertexBuffer");
		SetDebugName(
			filigreeHandIndexBuffer.Get(),
			"RealisticReflections.HandMirror.FiligreeMaskCover.IndexBuffer");
		SetDebugName(constantBuffer.Get(), "RealisticReflections.M5.Pane.ConstantBuffer");
		SetDebugName(samplerState.Get(), "RealisticReflections.M5.Pane.Sampler");
		SetDebugName(blendState.Get(), "RealisticReflections.M5.Pane.OpaqueBlend");
		SetDebugName(rasterizerState.Get(), "RealisticReflections.M5.Pane.NoCullRasterizer");
		SetDebugName(depthStencilState.Get(), "RealisticReflections.M5.Pane.DepthWrite");
		if (status)
			*status = InitializationStatus::kReady;
		return true;
	}

	void Renderer::Shutdown() noexcept
	{
		(void)ShutdownSafely();
	}

	bool Renderer::ShutdownSafely() noexcept
	{
		// Detach and zero every logical owner before the first native call.  A
		// corrupt object can then fault only its own Release; all remaining releases
		// are still attempted and Ready() is false before any vtable is entered.
		std::array<IUnknown*, kOwnedRendererResourceCount> detached{
			depthStencilState.Detach(),
			rasterizerState.Detach(),
			blendState.Detach(),
			samplerState.Detach(),
			constantBuffer.Detach(),
			customPaneVertexBuffer.Detach(),
			filigreeHandIndexBuffer.Detach(),
			filigreeHandVertexBuffer.Detach(),
			handIndexBuffer.Detach(),
			handVertexBuffer.Detach(),
			indexBuffer.Detach(),
			vertexBuffer.Detach(),
			pixelShader.Detach(),
			vertexShader.Detach(),
			inputLayout.Detach(),
			device.Detach()
		};
		activeSamplingMode = SamplingMode::kFullMipChain;
		coplanarFallbackReplacement = false;
		customPaneVertexCapacity = 0;
		bool faulted = false;
		for (std::uint32_t index = 0; index < detached.size(); ++index)
			ReleaseInterfaceSEH(
				detached[index], faulted, ReleaseDomain::kShutdown, index);
		return !faulted;
	}

	bool Renderer::Ready() const noexcept
	{
		return device && inputLayout && vertexShader && pixelShader && vertexBuffer &&
		       indexBuffer && handVertexBuffer && handIndexBuffer &&
		       filigreeHandVertexBuffer && filigreeHandIndexBuffer && constantBuffer &&
		       samplerState && blendState &&
		       rasterizerState && depthStencilState;
	}

	bool Renderer::TryGetSamplerDescription(
		D3D11_SAMPLER_DESC& a_description) const noexcept
	{
		a_description = {};
		if (!samplerState)
			return false;
		samplerState->GetDesc(&a_description);
		return true;
	}

	namespace
	{
		std::atomic<bool> g_shaderProbeStarted{ false };
		std::atomic<bool> g_shaderProbeDone{ false };
		WrapperShaderGetterProbe g_shaderProbe{};

		template <class Shader>
		__declspec(noinline) bool GetShaderSEH(
			ID3D11DeviceContext* context,
			void (ID3D11DeviceContext::*getter)(Shader**, ID3D11ClassInstance**, UINT*),
			Shader** shader,
			ID3D11ClassInstance** instances,
			UINT* count) noexcept
		{
			__try {
				(context->*getter)(shader, instances, count);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		__declspec(noinline) bool ReleaseProbeObjectSEH(
			IUnknown* object, std::uint32_t& exceptionCode) noexcept
		{
			__try {
				object->Release();
				return true;
			} __except (exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		template <class Shader>
		void ProbeShaderStage(
			WrapperShaderGetterProbe::Stage& stage,
			ID3D11DeviceContext* context,
			void (ID3D11DeviceContext::*getter)(Shader**, ID3D11ClassInstance**, UINT*)) noexcept
		{
			Shader* shader = nullptr;
			std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> instances{};
			UINT count = static_cast<UINT>(instances.size());
			stage = {};
			if (!GetShaderSEH(context, getter, &shader, instances.data(), &count)) {
				stage.getterFaulted = true;
				return;
			}
			stage.shader = reinterpret_cast<std::uintptr_t>(shader);
			stage.instanceCount = count;
			stage.shape = InspectComObject(shader, stage.shaderModule);
			if (stage.shape == 4) {
				stage.released = ReleaseProbeObjectSEH(shader, stage.releaseExceptionCode);
			}
			const UINT bounded = (std::min)(count, static_cast<UINT>(instances.size()));
			for (UINT index = 0; index < bounded; ++index) {
				std::uintptr_t module = 0;
				if (InspectComObject(instances[index], module) == 4) {
					std::uint32_t code = 0;
					(void)ReleaseProbeObjectSEH(instances[index], code);
				}
			}
		}

		void ProbeWrapperShaderGettersOnce(ID3D11DeviceContext* context) noexcept
		{
			if (!EngineDeviceIdentity::ForwardingDeviceAccepted() ||
				g_shaderProbeStarted.exchange(true, std::memory_order_acq_rel)) {
				return;
			}
			ProbeShaderStage(g_shaderProbe.stages[0], context, &ID3D11DeviceContext::VSGetShader);
			ProbeShaderStage(g_shaderProbe.stages[1], context, &ID3D11DeviceContext::PSGetShader);
			ProbeShaderStage(g_shaderProbe.stages[2], context, &ID3D11DeviceContext::GSGetShader);
			ProbeShaderStage(g_shaderProbe.stages[3], context, &ID3D11DeviceContext::HSGetShader);
			ProbeShaderStage(g_shaderProbe.stages[4], context, &ID3D11DeviceContext::DSGetShader);
			g_shaderProbe.done = true;
			g_shaderProbeDone.store(true, std::memory_order_release);
			static constexpr const char* kNames[5]{ "vertex", "pixel", "geometry", "hull", "domain" };
			for (std::size_t index = 0; index < 5; ++index) {
				const auto& stage = g_shaderProbe.stages[index];
				char line[240]{};
				std::snprintf(line, sizeof(line),
					"wrapped-context %sGetShader probe: getterFaulted=%u shader=0x%llx instances=%u shape=%u module=0x%llx released=%u releaseCode=0x%08x",
					kNames[index], static_cast<unsigned>(stage.getterFaulted),
					static_cast<unsigned long long>(stage.shader), stage.instanceCount,
					static_cast<unsigned>(stage.shape),
					static_cast<unsigned long long>(stage.shaderModule),
					static_cast<unsigned>(stage.released), stage.releaseExceptionCode);
				Emit(line);
			}
		}
	}

	WrapperShaderGetterProbe GetWrapperShaderGetterProbe() noexcept
	{
		return g_shaderProbeDone.load(std::memory_order_acquire) ? g_shaderProbe :
			WrapperShaderGetterProbe{};
	}

	void SetDiagnosticSink(const DiagnosticSink a_sink) noexcept
	{
		g_diagnosticSink.store(a_sink, std::memory_order_release);
	}

	const char* ToString(const SnapshotOrdinal a_ordinal) noexcept
	{
		switch (a_ordinal) {
		case SnapshotOrdinal::kPredicate: return "predicate";
		case SnapshotOrdinal::kDepthStencilView: return "depth-stencil-view";
		case SnapshotOrdinal::kRenderTarget0: return "render-target-0";
		case SnapshotOrdinal::kRenderTarget1: return "render-target-1";
		case SnapshotOrdinal::kRenderTarget2: return "render-target-2";
		case SnapshotOrdinal::kRenderTarget3: return "render-target-3";
		case SnapshotOrdinal::kRenderTarget4: return "render-target-4";
		case SnapshotOrdinal::kRenderTarget5: return "render-target-5";
		case SnapshotOrdinal::kRenderTarget6: return "render-target-6";
		case SnapshotOrdinal::kRenderTarget7: return "render-target-7";
		case SnapshotOrdinal::kDepthStencilState: return "depth-stencil-state";
		case SnapshotOrdinal::kBlendState: return "blend-state";
		case SnapshotOrdinal::kRasterizerState: return "rasterizer-state";
		case SnapshotOrdinal::kPixelSampler: return "pixel-sampler";
		case SnapshotOrdinal::kPixelResource0: return "pixel-resource-0";
		case SnapshotOrdinal::kPixelResource1: return "pixel-resource-1";
		case SnapshotOrdinal::kPixelConstantBuffer: return "pixel-constant-buffer";
		case SnapshotOrdinal::kVertexConstantBuffer: return "vertex-constant-buffer";
		case SnapshotOrdinal::kDomainShader: return "domain-shader";
		case SnapshotOrdinal::kDomainInstances: return "domain-class-instance";
		case SnapshotOrdinal::kHullShader: return "hull-shader";
		case SnapshotOrdinal::kHullInstances: return "hull-class-instance";
		case SnapshotOrdinal::kGeometryShader: return "geometry-shader";
		case SnapshotOrdinal::kGeometryInstances: return "geometry-class-instance";
		case SnapshotOrdinal::kPixelShader: return "pixel-shader";
		case SnapshotOrdinal::kPixelInstances: return "pixel-class-instance";
		case SnapshotOrdinal::kVertexShader: return "vertex-shader";
		case SnapshotOrdinal::kVertexInstances: return "vertex-class-instance";
		case SnapshotOrdinal::kIndexBuffer: return "index-buffer";
		case SnapshotOrdinal::kVertexBuffer: return "vertex-buffer";
		case SnapshotOrdinal::kInputLayout: return "input-layout";
		default: return "unknown";
		}
	}

	// The pane's bias exists to beat one coplanar surface -- the authored black
	// fallback in the same plane -- and must never beat a surface that is
	// genuinely in front of it. On the pointed arch the nearest such surface is
	// the spandrel, whose front face the generator places 2 units ahead of the
	// pane (`x_front=2.0`), so the budget is small and fixed in *world* units.
	//
	// A rasterizer bias is in NDC, and for a perspective projection one NDC step
	// is a world distance that grows with the square of the viewing distance:
	// dz_world/dz_ndc = z^2 / near. The shipped clamp of 2e-4 is therefore worth
	// a fraction of a unit at arm's length and tens of units across a courtyard,
	// which is precisely the owner's report -- wood up close, reflection far away.
	//
	// So the clamp is derived from the distance instead of fixed: solve for the
	// NDC value that is worth kCoplanarBiasWorldUnits at this distance, and cap
	// it at the shipped value so close-range behaviour is exactly what it was.
	// Half a unit is hundreds of depth ULPs at any distance the pane is visible
	// from, which is far more than coplanar rounding needs, and a quarter of the
	// spandrel's clearance.

	bool Renderer::EnsureCoplanarBias(float viewDistance) noexcept
	{
		if (!coplanarFallbackReplacement || !device)
			return true;
		if (!(viewDistance > 0.0f) || !std::isfinite(viewDistance))
			return true;
		float wanted = -(kAssumedNearPlane * kCoplanarBiasWorldUnits) /
			(viewDistance * viewDistance);
		if (wanted < kCoplanarBiasClampNear)
			wanted = kCoplanarBiasClampNear;  // never looser than what shipped
		// Quantize so ordinary walking does not rebuild the state every frame;
		// an eighth of a step is far below what any depth test can resolve.
		constexpr float kStep = 1.125f;
		float quantized = kCoplanarBiasClampNear;
		while (quantized < -1.0e-7f && quantized < wanted)
			quantized /= kStep;
		if (coplanarBiasClamp != 0.0f &&
			std::abs(quantized - coplanarBiasClamp) <= std::abs(quantized) * 1.0e-3f)
			return true;

		D3D11_RASTERIZER_DESC description{};
		description.FillMode = D3D11_FILL_SOLID;
		description.CullMode = D3D11_CULL_NONE;
		description.FrontCounterClockwise = FALSE;
		description.DepthBias = -4;
		description.DepthBiasClamp = quantized;
		description.SlopeScaledDepthBias =
			-2.0f / static_cast<float>(1u << D3D11_SUBPIXEL_FRACTIONAL_BIT_COUNT);
		description.DepthClipEnable = TRUE;
		description.ScissorEnable = FALSE;
		description.MultisampleEnable = FALSE;
		description.AntialiasedLineEnable = FALSE;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> replacement;
		if (FAILED(device->CreateRasterizerState(&description, replacement.GetAddressOf())))
			return false;  // keep the state we have
		SetDebugName(replacement.Get(), "RealisticReflections.M5.Pane.NoCullRasterizer");
		rasterizerState = std::move(replacement);
		coplanarBiasClamp = quantized;
		return true;
	}

	DrawStatus Renderer::Draw(
		ID3D11DeviceContext* a_context,
		const DrawRequest& input,
		bool* a_motionOutputEnabled) noexcept
	{
		DrawRequest backing{};
		if (input.opaqueBacking) {
			if (!input.worldSpaceDepth || !ValidPane(input.pane) ||
				!HandMirrorRuntimeBridgePolicy::IsValidOwner(input.pane.owner))
				return DrawStatus::kInvalidArgument;
			backing.pane = input.pane;
			backing.mainView = input.mainView;
			backing.targets = input.targets;
			backing.targets.motionRTV = nullptr;
			backing.customPaneTriangles = input.customPaneTriangles;
			backing.worldSpaceDepth = true;
			backing.opaqueBacking = true;
			// Geometry coverage uses the same view for both clip volumes. This is
			// material output and is never admitted to a publication store.
			backing.frame.owner = input.pane.owner;
			backing.frame.audience = HandMirrorRuntimeBridgePolicy::PublicationAudience::kInternalHandOnly;
			backing.frame.reflectedViewProjection = input.mainView.viewProjection;
			backing.frame.reflectedOrigin = input.mainView.origin;
			backing.frame.width = backing.frame.height = 1;
		}
		const auto& a_request = input.opaqueBacking ? backing : input;
		if (a_motionOutputEnabled)
			*a_motionOutputEnabled = false;
		if (!Ready())
			return DrawStatus::kNotInitialized;
		if (!a_context)
			return DrawStatus::kInvalidArgument;

		// Size the coplanar bias for the distance this pane is actually being
		// seen at, so it stays worth the same fraction of a world unit whether
		// the player is leaning into the glass or looking across a courtyard.
		{
			const auto toPane = DirectX::XMVectorSubtract(
				DirectX::XMLoadFloat3(&a_request.pane.center),
				DirectX::XMLoadFloat3(&a_request.mainView.origin));
			(void)EnsureCoplanarBias(
				DirectX::XMVectorGetX(DirectX::XMVector3Length(toPane)));
		}

		DrawQueryResources queryResources{};
		bool nativeQueryFaulted = false;
		ID3D11Device* rawContextDevice = nullptr;
		const bool contextDeviceRead = GetContextDeviceSEH(
			a_context, rawContextDevice, nativeQueryFaulted);
		queryResources.contextDevice.Attach(rawContextDevice);
		if (nativeQueryFaulted)
			return DrawStatus::kContextStateCaptureFailed;
		if (!contextDeviceRead ||
			!EngineDeviceIdentityPolicy::OwnedByEngineDevice(
				EngineDeviceIdentity::Current(
					reinterpret_cast<std::uintptr_t>(device.Get())),
				reinterpret_cast<std::uintptr_t>(queryResources.contextDevice.Get()))) {
			return DrawStatus::kDeviceMismatch;
		}
		ProbeWrapperShaderGettersOnce(a_context);

		if (!a_request.opaqueBacking && !ValidPublishedFrame(
				a_request.frame, device.Get(), queryResources.reflectionResource,
				nativeQueryFaulted))
			return nativeQueryFaulted ? DrawStatus::kContextStateCaptureFailed :
				DrawStatus::kNoPublishedFrame;
		if (!HandMirrorRuntimeBridgePolicy::SameOwner(
				a_request.frame.owner, a_request.pane.owner))
			return DrawStatus::kCandidateMismatch;
		const auto targetStatus = ValidateTargets(
			a_request.targets, device.Get(), queryResources.reflectionResource.Get(),
			queryResources.colorTargetResource,
			queryResources.depthTargetResource, nativeQueryFaulted);
		if (nativeQueryFaulted)
			return DrawStatus::kContextStateCaptureFailed;
		if (targetStatus != DrawStatus::kDrawn)
			return targetStatus;
		if (AnyStreamOutputTargetBound(a_context, nativeQueryFaulted))
			return DrawStatus::kStreamOutputBound;
		if (nativeQueryFaulted)
			return DrawStatus::kContextStateCaptureFailed;
		const bool motionTargetValid = ValidateMotionTarget(
			a_request.targets.motionRTV, device.Get(),
			queryResources.reflectionResource.Get(),
			queryResources.colorTargetResource.Get(),
			queryResources.depthTargetResource.Get(),
			queryResources.motionTargetResource, nativeQueryFaulted);
		if (nativeQueryFaulted)
			return DrawStatus::kContextStateCaptureFailed;
		const bool depthMotionRequested =
			a_request.motionMode == MotionMode::kDepthReconstructedWorld;
		const bool motionDepthValid = motionTargetValid && depthMotionRequested &&
			ValidateMotionDepth(
				a_request.frame.depthSRV.Get(), device.Get(), a_request.frame.width,
				a_request.frame.height,
				a_request.frame.owner.kind == HandMirrorRuntimeBridgePolicy::OwnerKind::kHand &&
					HandMirrorLoweredPresentationPolicy::reducedResolutionEnabled.load(std::memory_order_relaxed),
				queryResources.reflectionResource.Get(),
				queryResources.colorTargetResource.Get(),
				queryResources.depthTargetResource.Get(),
				queryResources.motionTargetResource.Get(),
				queryResources.reflectionDepthResource, nativeQueryFaulted);
		if (nativeQueryFaulted)
			return DrawStatus::kContextStateCaptureFailed;
		DirectX::XMFLOAT4X4 inverseReflectedStored{};
		DirectX::XMFLOAT4X4 previousInverseReflectedStored{};
		DirectX::XMStoreFloat4x4(
			&inverseReflectedStored, DirectX::XMMatrixIdentity());
		DirectX::XMStoreFloat4x4(
			&previousInverseReflectedStored, DirectX::XMMatrixIdentity());
		DirectX::XMFLOAT3 previousPaneNormal{};
		// The history must come from the immediately previous delivery call:
		// TAA's colour history is one main frame old, so a history that skipped
		// delivery frames (candidate/plane/projection rejects) spans several
		// frames of camera motion and would reproject reflected content to the
		// wrong screen position. Skipping the velocity write preserves native
		// motion for one frame instead of emitting a mis-scaled vector.
		const bool immediateHistoryValid =
			ValidImmediateMotionHistory(a_request);
		ShaderMotionMode shaderMotionMode = ShaderMotionMode::kDisabled;
		if (motionTargetValid &&
			a_request.motionMode == MotionMode::kRejectHistory) {
			// No history input is needed: the shader publishes an off-screen
			// velocity so TAA keeps the current pane colour.
			shaderMotionMode = ShaderMotionMode::kRejectHistory;
		} else if (motionTargetValid && immediateHistoryValid) {
			switch (a_request.motionMode) {
			case MotionMode::kDepthReconstructedWorld:
				if (motionDepthValid && a_request.frame.depthSRV &&
					Finite(a_request.motionHistory.reflectedViewProjection) &&
					Finite(a_request.motionHistory.reflectedOrigin) &&
					TryInvertFinite(
						a_request.frame.reflectedViewProjection,
						inverseReflectedStored) &&
					TryInvertFinite(
						a_request.motionHistory.reflectedViewProjection,
						previousInverseReflectedStored)) {
					const auto paneNormalVector = DirectX::XMVector3Normalize(
						DirectX::XMVector3Cross(
							DirectX::XMLoadFloat3(
								&a_request.motionHistory.pane.tangentExtent),
							DirectX::XMLoadFloat3(
								&a_request.motionHistory.pane.bitangentExtent)));
					DirectX::XMStoreFloat3(
						&previousPaneNormal, paneNormalVector);
					if (Finite(previousPaneNormal)) {
						shaderMotionMode =
							ShaderMotionMode::kDepthReconstructedWorld;
					}
				}
				break;
			case MotionMode::kPaneAffine:
				// No reflected-depth input is needed: the shader transports the
				// same authored pane-local point through the previous pane basis.
				shaderMotionMode = ShaderMotionMode::kPaneAffine;
				break;
			default:
				break;
			}
		}
		// Validate the mandatory colour/depth draw first.  Motion is an optional
		// temporal-quality output: an engine read binding of slot 7 must not turn a
		// valid reflection into a black pane.  The second scan below adds only the
		// motion resource, so an alias discovered there can safely degrade this draw
		// to RT0-only while mandatory output hazards still fail closed.
		const auto outputStatus = ValidateUntrackedOutputs(
			a_context, queryResources.colorTargetResource.Get(),
			queryResources.depthTargetResource.Get(), nullptr, nativeQueryFaulted);
		if (nativeQueryFaulted)
			return DrawStatus::kContextStateCaptureFailed;
		if (outputStatus != DrawStatus::kDrawn)
			return outputStatus;
		if (shaderMotionMode != ShaderMotionMode::kDisabled) {
			const auto motionOutputStatus = ValidateUntrackedOutputs(
				a_context, queryResources.colorTargetResource.Get(),
				queryResources.depthTargetResource.Get(),
				queryResources.motionTargetResource.Get(), nativeQueryFaulted);
			if (nativeQueryFaulted)
				return DrawStatus::kContextStateCaptureFailed;
			if (motionOutputStatus == DrawStatus::kShaderResourceAliasBound ||
				motionOutputStatus == DrawStatus::kComputeUAVAliasBound) {
				shaderMotionMode = ShaderMotionMode::kDisabled;
			} else if (motionOutputStatus != DrawStatus::kDrawn) {
				return motionOutputStatus;
			}
		}
		const bool motionEnabled =
			shaderMotionMode != ShaderMotionMode::kDisabled;
		const bool depthMotionEnabled =
			shaderMotionMode == ShaderMotionMode::kDepthReconstructedWorld;
		// Projection non-coverage can renew only the narrow M5 continuity lease.
		// Internal Hand coverage is clipped against the same style-selected 16- or
		// 24-gon that is drawn, so a square-corner overlap cannot report success.
		auto projectionStatus = ClassifyProjectionCoverage(
			a_request.pane, a_request.mainView, a_request.frame, a_request.customPaneTriangles);
		if (a_request.clampReflectedCoverage &&
			projectionStatus == DrawStatus::kProjectionReflectedUncovered)
			projectionStatus = DrawStatus::kDrawnPartialCoverage;
		if (!IsSuccessfulDraw(projectionStatus))
			return projectionStatus;
		if (!queryResources.Scrub())
			return DrawStatus::kContextStateCaptureFailed;

		const float halfTexelX = 0.5f / static_cast<float>(a_request.frame.width);
		const float halfTexelY = 0.5f / static_cast<float>(a_request.frame.height);
		DirectX::XMFLOAT4X4 previousMainViewProjection{};
		DirectX::XMStoreFloat4x4(
			&previousMainViewProjection, DirectX::XMMatrixIdentity());
		DirectX::XMFLOAT4X4 previousReflectedViewProjection{};
		DirectX::XMStoreFloat4x4(
			&previousReflectedViewProjection, DirectX::XMMatrixIdentity());
		DirectX::XMFLOAT3 previousMainOrigin{};
		DirectX::XMFLOAT3 previousReflectedOrigin{};
		PaneTransform previousPane{};
		if (immediateHistoryValid) {
			previousMainViewProjection =
				a_request.motionHistory.mainViewProjection;
			previousMainOrigin = a_request.motionHistory.mainOrigin;
			previousPane = a_request.motionHistory.pane;
		}
		if (depthMotionEnabled) {
			previousReflectedViewProjection =
				a_request.motionHistory.reflectedViewProjection;
			previousReflectedOrigin = a_request.motionHistory.reflectedOrigin;
		}
		const auto apertureKind = ResolvePaneApertureKind(a_request.frame);
		const bool customPane = !a_request.customPaneTriangles.empty();
		if (customPane && (apertureKind != PaneApertureKind::kWallQuad ||
			!MirrorNifContract::ValidTriangles(a_request.customPaneTriangles)))
			return DrawStatus::kInvalidArgument;
		// Keep small pane offsets away from large absolute world coordinates.
		// Otherwise worldPosition = center + offset rounds before the shader
		// subtracts the camera, defeating a bounded coplanar depth tolerance far
		// from the origin. All position uniforms share this same anchor, including
		// previous-frame motion positions; matrices and direction vectors do not
		// change. The default path keeps its exact previous constants.
		const auto positionConstant = [&](const DirectX::XMFLOAT3& position, float w) {
			if (!coplanarFallbackReplacement)
				return DirectX::XMFLOAT4{ position.x, position.y, position.z, w };
			return DirectX::XMFLOAT4{
				position.x - a_request.mainView.origin.x,
				position.y - a_request.mainView.origin.y,
				position.z - a_request.mainView.origin.z, w };
		};
		const PaneConstants constants{
			a_request.mainView.viewProjection,
			a_request.frame.reflectedViewProjection,
			positionConstant(a_request.mainView.origin, a_request.opaqueBacking ? 1.0f : 0.0f),
			positionConstant(a_request.frame.reflectedOrigin, a_request.clampReflectedCoverage ? 1.0f : 0.0f),
			positionConstant(a_request.pane.center, static_cast<float>(apertureKind)),
			{ a_request.pane.tangentExtent.x, a_request.pane.tangentExtent.y,
				a_request.pane.tangentExtent.z, 0.0f },
			{ a_request.pane.bitangentExtent.x, a_request.pane.bitangentExtent.y,
				a_request.pane.bitangentExtent.z, 0.0f },
			{ halfTexelX, halfTexelY, 1.0f - halfTexelX, 1.0f - halfTexelY },
			inverseReflectedStored,
			previousMainViewProjection,
			previousReflectedViewProjection,
			previousInverseReflectedStored,
			positionConstant(previousMainOrigin, 0.0f),
			positionConstant(previousReflectedOrigin, 0.0f),
			positionConstant(previousPane.center, 0.0f),
			{ previousPane.tangentExtent.x, previousPane.tangentExtent.y,
				previousPane.tangentExtent.z, 0.0f },
			{ previousPane.bitangentExtent.x, previousPane.bitangentExtent.y,
				previousPane.bitangentExtent.z, 0.0f },
			{ previousPaneNormal.x, previousPaneNormal.y, previousPaneNormal.z,
				static_cast<float>(shaderMotionMode) },
			{ 1.0f / a_request.targets.viewport.Width,
				1.0f / a_request.targets.viewport.Height,
				a_request.targets.viewport.TopLeftX,
				a_request.targets.viewport.TopLeftY }
		};

		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(a_context->Map(
				constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			return DrawStatus::kConstantBufferMapFailed;
		CopyMappedConstantsWithSEHUnmap(
			a_context, constantBuffer.Get(), mapped.pData, &constants, sizeof(constants));
		if (customPane) {
			const auto count = static_cast<UINT>(a_request.customPaneTriangles.size());
			static_assert(sizeof(Vertex) == sizeof(DirectX::XMFLOAT2));
			if (!customPaneVertexBuffer || customPaneVertexCapacity < count) {
				D3D11_BUFFER_DESC description{};
				description.ByteWidth = count*static_cast<UINT>(sizeof(Vertex));
				description.Usage = D3D11_USAGE_DYNAMIC;
				description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
				description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
				Microsoft::WRL::ComPtr<ID3D11Buffer> replacement;
				if (FAILED(device->CreateBuffer(&description,nullptr,replacement.GetAddressOf())))
					return DrawStatus::kCustomVertexBufferCreationFailed;
				customPaneVertexBuffer = std::move(replacement);
				customPaneVertexCapacity = count;
			}
			D3D11_MAPPED_SUBRESOURCE vertices{};
			if (FAILED(a_context->Map(customPaneVertexBuffer.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&vertices)))
				return DrawStatus::kCustomVertexBufferMapFailed;
			CopyMappedConstantsWithSEHUnmap(a_context,customPaneVertexBuffer.Get(),vertices.pData,
				a_request.customPaneTriangles.data(),a_request.customPaneTriangles.size_bytes());
		}

		ContextStateSnapshot savedState{ a_context };
		if (!savedState.Captured())
			return DrawStatus::kContextStateCaptureFailed;
		const bool internalHandPane =
			apertureKind != PaneApertureKind::kWallQuad;
		ID3D11Buffer* selectedVertexBuffer = vertexBuffer.Get();
		ID3D11Buffer* selectedIndexBuffer = indexBuffer.Get();
		UINT selectedIndexCount = static_cast<UINT>(kIndices.size());
		if (customPane) {
			selectedVertexBuffer = customPaneVertexBuffer.Get();
			selectedIndexBuffer = nullptr;
			selectedIndexCount = static_cast<UINT>(a_request.customPaneTriangles.size());
		} else if (apertureKind == PaneApertureKind::kHandRound16) {
			selectedVertexBuffer = handVertexBuffer.Get();
			selectedIndexBuffer = handIndexBuffer.Get();
			selectedIndexCount = static_cast<UINT>(kHandCoverIndices.size());
		} else if (apertureKind == PaneApertureKind::kHandFiligree24) {
			selectedVertexBuffer = filigreeHandVertexBuffer.Get();
			selectedIndexBuffer = filigreeHandIndexBuffer.Get();
			selectedIndexCount = static_cast<UINT>(kHandCoverIndices.size());
		}
		const DrawBindings bindings{
			inputLayout.Get(),
			selectedVertexBuffer,
			selectedIndexBuffer,
			selectedIndexCount,
			vertexShader.Get(),
			pixelShader.Get(),
			constantBuffer.Get(),
			a_request.frame.colorSRV.Get(),
			depthMotionEnabled ? a_request.frame.depthSRV.Get() : nullptr,
			samplerState.Get(),
			rasterizerState.Get(),
			blendState.Get(),
			depthStencilState.Get(),
			a_request.targets.colorRTV,
			motionEnabled ? a_request.targets.motionRTV : nullptr,
			internalHandPane && !a_request.worldSpaceDepth ? nullptr : a_request.targets.depthDSV,
			&a_request.targets.viewport,
			TouchTessellationStages()
		};
		bool restoreSucceeded = false;
		IssuePaneDrawWithSEHRestore(
			a_context, &bindings, &savedState, &restoreSucceeded);
		if (!restoreSucceeded)
			return DrawStatus::kContextStateRestoreFailed;
		if (a_motionOutputEnabled)
			*a_motionOutputEnabled = motionEnabled;
		return projectionStatus;
	}
}

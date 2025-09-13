#include "framework.h"
#include "imgui.h"
#include <taskflow/taskflow.hpp>
#include "taskflow/algorithm/for_each.hpp"
#include "entt/entt.hpp"
#include <bit>

struct BindBuffer
{
	int set;
	int bind;
	HGEGraphics::Buffer* buffer;
};

struct BindTexture
{
	int set;
	int bind;
	HGEGraphics::Texture* texture;
};

struct BindSampler
{
	int set;
	int bind;
	CGPUSamplerId sampler;
};

struct Material
{
	oval_device_t* device;
	HGEGraphics::Shader* shader;
	std::vector<BindBuffer> buffers;
	std::vector<BindTexture> textures;
	std::vector<BindSampler> samplers;

	template<typename T>
	void bindBuffer(int set, int bind, const T& data)
	{
		auto size = sizeof(T);
		auto align_size = std::bit_ceil(size);
		auto desc = CGPUBufferDescriptor{
			.size = align_size,
			.name = "Material Data Buffer",
			.descriptors = CGPU_RESOURCE_TYPE_UNIFORM_BUFFER,
			.memory_usage = CGPU_MEMORY_USAGE_CPU_TO_GPU,
		};
		auto buffer = oval_create_buffer(device, &desc);
		cgpu_buffer_map(buffer->handle, nullptr);
		memcpy(buffer->handle->info->cpu_mapped_address, &data, size);
		cgpu_buffer_unmap(buffer->handle);
		buffers.emplace_back(set, bind, buffer);
	}

	void free()
	{
		textures.clear();
		samplers.clear();
		for (auto& bind : buffers)
		{
			oval_free_buffer(device, bind.buffer);
		}
		buffers.clear();
		shader = nullptr;
		device = nullptr;
	}
};

void bindMaterial(HGEGraphics::RenderPassEncoder* encoder, const Material& material)
{
	for (auto& bind : material.buffers)
		set_global_buffer(encoder, bind.buffer, bind.set, bind.bind);
	for (auto& bind : material.textures)
		set_global_texture(encoder, bind.texture, bind.set, bind.bind);
	for (auto& bind : material.samplers)
		set_global_sampler(encoder, bind.sampler, bind.set, bind.bind);
}

struct Position
{
	HMM_Vec3 value;
};

struct Rotation
{
	HMM_Quat value;
};

struct SimpleHarmonic
{
	HMM_Vec3 amplitude;
	float speed = 0;
	HMM_Vec3 base;
};

struct Rotate
{
	HMM_Vec3 axis;
	float speed = 0;
	HMM_Quat base;
};

struct MoveInterpolation
{
	HMM_Vec3 value;
};

struct RotateInterpolation
{
	HMM_Quat value;
};

struct Matrix
{
	HMM_Mat4 model;
};

struct ShowMatrix
{
	HMM_Mat4 model;
};

struct Rendable
{
	int material;
	int mesh;
};

struct Light
{
	HMM_Vec4 color;
};

struct Camera
{
	float fov;
	float nearPlane;
	float farPlane;
	int width;
	int height;
};

struct CameraMatrix
{
	HMM_Mat4 view;
	HMM_Mat4 proj;
};

struct PassData
{
	HMM_Mat4	vpMatrix;
	HMM_Vec4	lightDir;
	HMM_Vec4	viewPos;
};

struct MaterialData
{
	HMM_Vec4	shininess;
	HMM_Vec4	albedo;
};

struct RenderObject
{
	int material;
	int mesh;
	HMM_Mat4 wMatrix;
};

struct ObjectData
{
	HMM_Mat4	wMatrix;
};

struct ViewRenderPacket
{
	PassData passData;
	std::pmr::vector<RenderObject> renderObjects;
	std::pmr::vector<ObjectData> renderData;
};

struct SystemContext
{
	float delta_time;
	float time_since_startup;
	double delta_time_double;
	double time_since_startup_double;
	float interpolation_time;
	double interpolation_time_double;
};

void doSimpleHarmonicMove(const SystemContext& context, const SimpleHarmonic& simpleHarmonic, Position& position)
{
	position.value = simpleHarmonic.amplitude * sin(simpleHarmonic.speed * context.time_since_startup) + simpleHarmonic.base;
}

void doRotation(const SystemContext& context, const Rotate& rotate, Rotation& rotation)
{
	rotation.value = rotate.base * HMM_QFromAxisAngle_LH(rotate.axis, context.time_since_startup * rotate.speed);
}

void updateMatrixPositionOnly(const SystemContext& context, const Position& position, Matrix& matrix)
{
	matrix.model = HMM_Translate(position.value);
}

void updateMatrixRotationOnly(const SystemContext& context, const Rotation& rotation, Matrix& matrix)
{
	matrix.model = HMM_QToM4(rotation.value);
}

void updateMatrixPositionAndRotation(const SystemContext& context, const Position& position, const Rotation& rotation, Matrix& matrix)
{
	matrix.model = HMM_TRS(position.value, rotation.value, HMM_V3_One);
}

void updateMoveInterpolation(const SystemContext& context, const SimpleHarmonic& simpleHarmonic, MoveInterpolation& moveInterp)
{
	auto pos1 = simpleHarmonic.amplitude* sin(simpleHarmonic.speed * context.time_since_startup) + simpleHarmonic.base;
	auto pos2 = simpleHarmonic.amplitude * sin(simpleHarmonic.speed * (context.time_since_startup + context.interpolation_time)) + simpleHarmonic.base;
	moveInterp.value = pos2 - pos1;
}

void updateRotateInterpolation(const SystemContext& context, const Rotate& rotate, RotateInterpolation& rotateInterp)
{
	auto rot1 = rotate.base * HMM_QFromAxisAngle_LH(rotate.axis, context.time_since_startup * rotate.speed);
	auto rot2 = rotate.base * HMM_QFromAxisAngle_LH(rotate.axis, (context.time_since_startup + context.interpolation_time) * rotate.speed);
	rotateInterp.value = HMM_MulQ(HMM_InvQ(rot1), rot2);
}

void updateShowMatrixStatic(const SystemContext& context, const Matrix& matrix, ShowMatrix& showMatrix)
{
	showMatrix.model = matrix.model;
}

void updateShowMatrixMoveOnly(const SystemContext& context, const Matrix& matrix, const MoveInterpolation& moveInterp, ShowMatrix& showMatrix)
{
	showMatrix.model = HMM_MulM4(matrix.model, HMM_Translate(moveInterp.value));
}

void updateShowMatrixRotateOnly(const SystemContext& context, const Matrix& matrix, const RotateInterpolation& rotateInterp, ShowMatrix& showMatrix)
{
	auto oriPosition = HMM_M4GetTranslate(matrix.model);
	auto oriRotation = HMM_M4ToQ_RH(matrix.model);
	auto newRotation = HMM_MulQ(oriRotation, rotateInterp.value);
	showMatrix.model = HMM_TRS(oriPosition, newRotation, HMM_V3_One);
}

void updateShowMatrixMoveAndRotate(const SystemContext& context, const Matrix& matrix, const MoveInterpolation& moveInterp, const RotateInterpolation& rotateInterp, ShowMatrix& showMatrix)
{
	auto oriPosition = HMM_M4GetTranslate(matrix.model);
	auto oriRotation = HMM_M4ToQ_RH(matrix.model);
	auto newPosition = oriPosition + moveInterp.value;
	auto newRotation = HMM_MulQ(oriRotation, rotateInterp.value);
	showMatrix.model = HMM_TRS(newPosition, newRotation, HMM_V3_One);
}

struct FrameRenderPacket
{
	std::pmr::synchronized_pool_resource* memory_resource;
	std::pmr::vector<ViewRenderPacket> viewDatas;

	FrameRenderPacket(std::pmr::synchronized_pool_resource* memory_resource)
		: memory_resource(memory_resource), viewDatas(memory_resource)
	{
	}

	void clear()
	{
		viewDatas.clear();
	}
};

struct Application
{
	oval_device_t* device{ nullptr };
	entt::registry registry;
	std::vector<HGEGraphics::Mesh*> meshes;
	std::vector<HGEGraphics::Shader*> shaders;
	CGPUSamplerId texture_sampler = CGPU_NULLPTR;
	HGEGraphics::Texture* color_map{ nullptr };
	std::vector<Material> materials;
	std::pmr::synchronized_pool_resource root_memory_resource;
	std::array<FrameRenderPacket, 2> frameRenderPackets;
	size_t currentPackFrame{ 0 };
	tf::Executor taskExecutor{ (size_t)std::max((int)std::thread::hardware_concurrency() - 2, 1) };

	Application()
		: frameRenderPackets{ FrameRenderPacket{&root_memory_resource}, FrameRenderPacket{&root_memory_resource} }
	{
		void* buffer = nullptr;
		buffer = frameRenderPackets[0].memory_resource->allocate(1024 * 1024, 8);
		frameRenderPackets[0].memory_resource->deallocate(buffer, 8);
		buffer = frameRenderPackets[1].memory_resource->allocate(1024 * 1024, 8);
		frameRenderPackets[1].memory_resource->deallocate(buffer, 8);
	}
};

void _init_resource(Application& app)
{
	auto sphere = oval_load_mesh(app.device, "media/models/Sphere.obj");
	app.meshes.push_back(sphere);

	CGPUBlendAttachmentState blend_attachments = {
	.enable = false,
	.src_factor = CGPU_BLEND_FACTOR_ONE,
	.dst_factor = CGPU_BLEND_FACTOR_ZERO,
	.src_alpha_factor = CGPU_BLEND_FACTOR_ONE,
	.dst_alpha_factor = CGPU_BLEND_FACTOR_ZERO,
	.blend_op = CGPU_BLEND_OP_ADD,
	.blend_alpha_op = CGPU_BLEND_OP_ADD,
	.color_mask = CGPU_COLOR_MASK_RGBA,
	};
	CGPUBlendStateDescriptor blend_desc = {
		.attachment_count = 1,
		.p_attachments = &blend_attachments,
		.alpha_to_coverage = false,
		.independent_blend = false,
	};
	CGPUDepthStateDescriptor depth_desc = {
		.depth_test = true,
		.depth_write = true,
		.depth_op = CGPU_COMPARE_OP_GREATER_EQUAL,
		.stencil_test = false,
	};
	CGPURasterizerStateDescriptor rasterizer_state = {
		.cull_mode = CGPU_CULL_MODE_BACK,
	};
	auto shader = oval_create_shader(app.device, "shaderbin/obj2.vert.spv", "shaderbin/obj2.frag.spv", blend_desc, depth_desc, rasterizer_state);
	app.shaders.push_back(shader);

	CGPUSamplerDescriptor texture_sampler_desc = {
		.min_filter = CGPU_FILTER_TYPE_LINEAR,
		.mag_filter = CGPU_FILTER_TYPE_LINEAR,
		.mipmap_mode = CGPU_MIP_MAP_MODE_LINEAR,
		.address_u = CGPU_ADDRESS_MODE_REPEAT,
		.address_v = CGPU_ADDRESS_MODE_REPEAT,
		.address_w = CGPU_ADDRESS_MODE_REPEAT,
		.mip_lod_bias = 0,
		.max_anisotropy = 1,
	};
	app.texture_sampler = oval_create_sampler(app.device, &texture_sampler_desc);

	app.color_map = oval_load_texture(app.device, "media/textures/tex.jpg", true);

	app.materials.push_back({});
	auto& last = app.materials.back();
	last.device = app.device;
	last.shader = app.shaders[0];
	last.textures.emplace_back(1, 1, app.color_map);
	last.samplers.emplace_back(1, 2, app.texture_sampler);
	auto materialData = MaterialData{
		.shininess = HMM_V4(90, 0, 0, 0),
		.albedo = HMM_V4(1, 1, 1, 1),
	};
	last.bindBuffer(1, 0, materialData);
}

void _free_resource(Application& app)
{
	for (auto& mat : app.materials)
	{
		mat.free();
	}
	app.materials.clear();

	oval_free_texture(app.device, app.color_map);
	app.color_map = nullptr;

	oval_free_sampler(app.device, app.texture_sampler);
	app.texture_sampler = nullptr;

	for (auto mesh : app.meshes)
	{
		oval_free_mesh(app.device, mesh);
	}
	app.meshes.clear();

	for (auto shader : app.shaders)
	{
		oval_free_shader(app.device, shader);
	}
	app.shaders.clear();
}

void _init_world(Application& app)
{
	auto& registry = app.registry;
	auto e1 = registry.create();
	registry.emplace<Rotation>(e1, HMM_Q_Identity);
	registry.emplace<Matrix>(e1, HMM_M4_Identity);
	registry.emplace<Rotate>(e1, HMM_V3(1.0f, 0.0f, 0.0f), 1.0f, HMM_Q_Identity);
	registry.emplace<Rendable>(e1, 0, 0);
	registry.emplace<RotateInterpolation>(e1, HMM_Q_Identity);
	registry.emplace<ShowMatrix>(e1, HMM_M4_Identity);

	auto e2 = registry.create();
	registry.emplace<Position>(e2, HMM_V3_Zero);
	registry.emplace<Matrix>(e2, HMM_M4_Identity);
	registry.emplace<SimpleHarmonic>(e2, HMM_V3(1.5f, 0.0f, 0.0f), 1.0f, HMM_V3(0.0f, 0.0f, 0.0f));
	registry.emplace<Rendable>(e2, 0, 0);
	registry.emplace<MoveInterpolation>(e2);
	registry.emplace<ShowMatrix>(e2, HMM_M4_Identity);

	auto e3 = registry.create();
	registry.emplace<Position>(e3, HMM_V3_Zero);
	registry.emplace<Rotation>(e3, HMM_Q_Identity);
	registry.emplace<Matrix>(e3, HMM_M4_Identity);
	registry.emplace<Rotate>(e3, HMM_V3(1.0f, 0.0f, 0.0f), 0.5f, HMM_Q_Identity);
	registry.emplace<SimpleHarmonic>(e3, HMM_V3(0.0f, 1.5f, 0.0f), 2.0f, HMM_V3(0.0f, 0.0f, 0.0f));
	registry.emplace<Rendable>(e3, 0, 0);
	registry.emplace<RotateInterpolation>(e3, HMM_Q_Identity);
	registry.emplace<MoveInterpolation>(e3);
	registry.emplace<ShowMatrix>(e3, HMM_M4_Identity);

	auto cam = registry.create();
	auto cameraParentMat = HMM_QToM4(HMM_QFromEuler_YXZ(HMM_AngleDeg(33.4), HMM_AngleDeg(45), 0));
	auto cameraLocalMat = HMM_Translate(HMM_V3(0, 0, -5));
	auto cameraWMat = HMM_Mul(cameraParentMat, cameraLocalMat);
	registry.emplace<Matrix>(cam, cameraWMat);
	registry.emplace<Camera>(cam, 45.0f, 0.1f, 20.f, app.device->descriptor.width, app.device->descriptor.height);

	auto light = registry.create();
	auto lightDir = HMM_Norm(HMM_V3(0.25f, -0.7f, 1.25f));
	auto lightMat = HMM_PoseAt_LH(HMM_V3_Zero, lightDir, HMM_V3_Up);
	registry.emplace<Matrix>(light, lightMat);
	registry.emplace<Light>(light, HMM_V4(1.0f, 1.0f, 1.0f, 1.0f));
}

template<typename...T, typename Func>
tf::Task createForeachTask(const SystemContext& context, entt::registry& registry, tf::Taskflow& taskFlow, Func&& func)
{
	auto view = registry.view<T...>();
	return taskFlow.emplace([view, context, func](tf::Subflow& subflow) {
		subflow.for_each(view.begin(), view.end(), [view, context, func](auto entity)
			{
				auto args = std::tuple_cat(std::forward_as_tuple(context), view.get(entity));
				std::apply(std::forward<Func>(func), std::move(args));
			});
		});
}

template<typename...T, typename...Exclude, typename Func>
tf::Task createForeachTask(const SystemContext& context, entt::registry& registry, tf::Taskflow& taskFlow, Func&& func, entt::exclude_t<Exclude...> exclude)
{
	auto view = registry.view<T...>(exclude);
	return taskFlow.emplace([view, context, func](tf::Subflow& subflow) {
		subflow.for_each(view.begin(), view.end(), [view, context, func](auto entity)
			{
				auto args = std::tuple_cat(std::forward_as_tuple(context), view.get(entity));
				std::apply(std::forward<Func>(func), std::move(args));
			});
		});
}

tf::Task simulate(Application& app, tf::Taskflow& flow)
{
	auto& registry = app.registry;
	SystemContext context = SystemContext{ app.device->delta_time, app.device->time_since_startup, app.device->delta_time_double, app.device->time_since_startup_double, 0, 0 };
	auto simpleHarmonicMoveSystem = createForeachTask<const SimpleHarmonic, Position>(context, registry, flow, doSimpleHarmonicMove).name("简谐运动");
	auto rotationSystem = createForeachTask<const Rotate, Rotation>(context, registry, flow, doRotation).name("旋转运动");
	auto updateMatrixPositionOnlySystem = createForeachTask<const Position, Matrix>(context, registry, flow, updateMatrixPositionOnly, entt::exclude<Rotation>).name("更新矩阵PositionOnly");
	auto updateMatrixRotationOnlySystem = createForeachTask<const Rotation, Matrix>(context, registry, flow, updateMatrixRotationOnly, entt::exclude<Position>).name("更新矩阵RotationOnly");
	auto updateMatrixPositionAndRotationSystem = createForeachTask<const Position, const Rotation, Matrix>(context, registry, flow, updateMatrixPositionAndRotation).name("更新矩阵PositionAndRotation");

	auto beforeUpdateMatrix = flow.placeholder();
	beforeUpdateMatrix
		.succeed(simpleHarmonicMoveSystem, rotationSystem)
		.precede(updateMatrixPositionOnlySystem, updateMatrixRotationOnlySystem, updateMatrixPositionAndRotationSystem);

	auto endOfSimulate = flow.placeholder();
	endOfSimulate.succeed(updateMatrixPositionOnlySystem, updateMatrixRotationOnlySystem, updateMatrixPositionAndRotationSystem);

	return endOfSimulate;
}

std::pmr::vector<entt::entity> vis(Application& app, const Camera& camera, std::pmr::synchronized_pool_resource* memory_resource)
{
	auto& registry = app.registry;
	auto view = registry.view<const ShowMatrix, const Rendable>();
	std::pmr::vector<entt::entity> visibles(memory_resource);
	visibles.reserve(view.size_hint());
	for (auto [entity, matrix, rendable] : view.each())
	{
		bool visible = true;
		if (visible)
		{
			visibles.push_back(entity);
		}
	}
	return visibles;
}

std::pmr::vector<RenderObject> extract(Application& app, std::pmr::vector<entt::entity> visibles, std::pmr::synchronized_pool_resource* memory_resource)
{
	std::pmr::vector<RenderObject> renderObjects(memory_resource);
	renderObjects.reserve(visibles.size());
	auto& registry = app.registry;
	for (auto entity : visibles)
	{
		auto matrix = registry.get<ShowMatrix>(entity);
		auto rendable = registry.get<Rendable>(entity);
		RenderObject robj = {
			.material = rendable.material,
			.mesh = rendable.mesh,
			.wMatrix = matrix.model,
		};
		renderObjects.push_back(robj);
	}
	return renderObjects;
}

tf::Task interpolate(Application& app, tf::Taskflow& flow)
{
	auto& registry = app.registry;
	SystemContext context = SystemContext{ app.device->delta_time, app.device->time_since_startup, app.device->delta_time_double, app.device->time_since_startup_double, app.device->render_interpolation_time, app.device->render_interpolation_time_double };
	auto simpleHarmonicMoveSystem = createForeachTask<const SimpleHarmonic, MoveInterpolation>(context, registry, flow, updateMoveInterpolation).name("简谐运动");
	auto rotationSystem = createForeachTask<const Rotate, RotateInterpolation>(context, registry, flow, updateRotateInterpolation).name("旋转运动");
	auto updateMatrixStaticSystem = createForeachTask<const Matrix, ShowMatrix>(context, registry, flow, updateShowMatrixStatic, entt::exclude<MoveInterpolation, RotateInterpolation>).name("更新矩阵Static");
	auto updateMatrixPositionOnlySystem = createForeachTask<const Matrix, const MoveInterpolation, ShowMatrix>(context, registry, flow, updateShowMatrixMoveOnly, entt::exclude<RotateInterpolation>).name("更新矩阵PositionOnly");
	auto updateMatrixRotationOnlySystem = createForeachTask<const Matrix, const RotateInterpolation, ShowMatrix>(context, registry, flow, updateShowMatrixRotateOnly, entt::exclude<MoveInterpolation>).name("更新矩阵RotationOnly");
	auto updateMatrixPositionAndRotationSystem = createForeachTask<const Matrix, const MoveInterpolation, const RotateInterpolation, ShowMatrix>(context, registry, flow, updateShowMatrixMoveAndRotate).name("更新矩阵PositionAndRotation");

	auto beforeUpdateMatrix = flow.placeholder();
	beforeUpdateMatrix
		.succeed(simpleHarmonicMoveSystem, rotationSystem)
		.precede(updateMatrixPositionOnlySystem, updateMatrixRotationOnlySystem, updateMatrixPositionAndRotationSystem);

	auto endOfSimulate = flow.placeholder();
	endOfSimulate.succeed(updateMatrixPositionOnlySystem, updateMatrixRotationOnlySystem, updateMatrixPositionAndRotationSystem);

	return endOfSimulate;

}

void enumViews(Application& app)
{
	auto& registry = app.registry;
	auto& currentFramePack = app.frameRenderPackets[app.currentPackFrame];

	auto lightDir = HMM_Norm(HMM_V3(0, -1, 0));

	auto light_view = registry.view<Light, const Matrix>();
	for (auto [entity, light, matrix] : light_view.each())
	{
		auto forward = HMM_M4GetForward(matrix.model);
		lightDir = forward;
		break;
	}

	currentFramePack.clear();
	auto camera_view = registry.view<Camera, const Matrix>();
	for (auto [entity, camera, matrix] : camera_view.each())
	{
		auto cameraMat = matrix.model;

		auto eye = HMM_M4GetTranslate(cameraMat);
		auto forward = HMM_M4GetForward(cameraMat);
		auto right = HMM_M4GetRight(cameraMat);
		auto viewMat = HMM_LookAt2_LH(eye, forward, HMM_V3_Up);

		float aspect = (float)camera.width / camera.height;
		float near = camera.nearPlane;
		float far = camera.farPlane;
		auto proj = HMM_Perspective_LH_RO(camera.fov * HMM_DegToRad, aspect, near, far);
		auto vpMat = proj * viewMat;

		currentFramePack.viewDatas.emplace_back(ViewRenderPacket{
			.passData = {
				.vpMatrix = vpMat,
				.lightDir = lightDir,
				.viewPos = HMM_V4V(eye, 0),
			},
			.renderObjects = std::move(extract(app, std::move(vis(app, camera, currentFramePack.memory_resource)), currentFramePack.memory_resource)),
			});
	}
}

void prepare(Application& app)
{
	auto& lastFrameRenderPacket = app.frameRenderPackets[app.currentPackFrame];

	for (auto& view : lastFrameRenderPacket.viewDatas)
	{
		std::sort(view.renderObjects.begin(), view.renderObjects.end(), [](const RenderObject& a, const RenderObject& b)
			{
				if (a.material != b.material)
					return a.material > b.material;
				else
					return a.mesh > b.mesh;
			});

		view.renderData.resize(view.renderObjects.size());
		for (size_t i = 0; i < view.renderObjects.size(); ++i)
		{
			view.renderData[i].wMatrix = view.renderObjects[i].wMatrix;
		}
	}
}

void submit(Application& app, FrameRenderPacket& lastFrameRenderPacket, oval_device_t* device, HGEGraphics::rendergraph_t& rg, HGEGraphics::texture_handle_t rg_back_buffer)
{
	using namespace HGEGraphics;

	auto depth_handle = rendergraph_declare_texture(&rg);
	rg_texture_set_extent(&rg, depth_handle, rg_texture_get_width(&rg, rg_back_buffer), rg_texture_get_height(&rg, rg_back_buffer));
	rg_texture_set_depth_format(&rg, depth_handle, DepthBits::D24, true);

	bool firstView = true;
	for (auto& view : lastFrameRenderPacket.viewDatas)
	{
		auto pass_ubo_handle = rendergraph_declare_uniform_buffer_quick(&rg, sizeof(PassData), &view.passData);
		auto object_ubo_handle = rendergraph_declare_uniform_buffer_quick(&rg, view.renderData.size() * sizeof(ObjectData), view.renderData.data());

		auto passBuilder = rendergraph_add_renderpass(&rg, "Main Pass");
		uint32_t color = 0xff000000;
		renderpass_add_color_attachment(&passBuilder, rg_back_buffer, firstView ? ECGPULoadAction::CGPU_LOAD_ACTION_CLEAR : ECGPULoadAction::CGPU_LOAD_ACTION_LOAD, color, ECGPUStoreAction::CGPU_STORE_ACTION_STORE);
		renderpass_add_depth_attachment(&passBuilder, depth_handle, CGPU_LOAD_ACTION_CLEAR, 0, CGPU_STORE_ACTION_DISCARD, CGPU_LOAD_ACTION_CLEAR, 0, CGPU_STORE_ACTION_DISCARD);
		renderpass_use_buffer(&passBuilder, pass_ubo_handle);
		renderpass_use_buffer(&passBuilder, object_ubo_handle);

		struct MainPassPassData
		{
			Application* app;
			ViewRenderPacket* view;
			HGEGraphics::buffer_handle_t pass_ubo_handle;
			HGEGraphics::buffer_handle_t object_ubo_handle;
		};
		MainPassPassData* passdata;
		renderpass_set_executable(&passBuilder, [](RenderPassEncoder* encoder, void* passdata)
			{
				MainPassPassData* resolved_passdata = (MainPassPassData*)passdata;
				Application& app = *resolved_passdata->app;
				set_global_dynamic_buffer(encoder, resolved_passdata->pass_ubo_handle, 0, 0);
				Material* lastMaterial = nullptr;
				for (size_t i = 0; i < resolved_passdata->view->renderObjects.size(); ++i)
				{
					auto& obj = resolved_passdata->view->renderObjects[i];
					auto& material = app.materials[obj.material];
					if (lastMaterial != &material)
					{
						bindMaterial(encoder, material);
						lastMaterial = &material;
					}
					set_global_buffer_with_offset_size(encoder, resolved_passdata->object_ubo_handle, 2, 0, i * sizeof(ObjectData), sizeof(ObjectData));
					draw(encoder, material.shader, app.meshes[obj.mesh]);
				}
			}, sizeof(MainPassPassData), (void**)&passdata);
		passdata->app = &app;
		passdata->view = &view;
		passdata->pass_ubo_handle = pass_ubo_handle;
		passdata->object_ubo_handle = object_ubo_handle;

		firstView = false;
	}
}

void on_update(oval_device_t* device)
{
	Application& app = *(Application*)device->descriptor.userdata;
	app.currentPackFrame = (app.currentPackFrame + 1) % 2;

	tf::Taskflow flow;
	auto simulateTask = simulate(app, flow);

	app.taskExecutor.run(flow).wait();
}

void on_imgui(oval_device_t* device)
{
	ImGui::Text("Hello, ImGui!");
	if (ImGui::Button("Capture"))
		oval_render_debug_capture(device);

	uint32_t length;
	const char** names;
	const float* durations;
	oval_query_render_profile(device, &length, &names, &durations);
	if (length > 0)
	{
		float total_duration = 0.f;
		for (uint32_t i = 0; i < length; ++i)
		{
			float duration = durations[i] * 1000;
			ImGui::Text("%s %7.2f us", names[i], duration);
			total_duration += duration;
		}
		ImGui::Text("Total Time: %7.2f us", total_duration);
	}
}

void on_draw(oval_device_t* device, HGEGraphics::rendergraph_t& rg, HGEGraphics::texture_handle_t rg_back_buffer)
{
	Application* app = (Application*)device->descriptor.userdata;
	//printf("draw:%lf\n", device->time_since_startup_double + device->render_interpolation_time_double);

	tf::Taskflow flow;

	auto simulateTask = interpolate(*app, flow);

	app->taskExecutor.run(flow).wait();

	enumViews(*app);
	prepare(*app);

	auto& lastFrameRenderPacket = app->frameRenderPackets[app->currentPackFrame];
	submit(*app, lastFrameRenderPacket, device, rg, rg_back_buffer);
}

extern "C"
int SDL_main(int argc, char *argv[])
{
	const int width = 800;
	const int height = 600;
	Application app;
	oval_device_descriptor device_descriptor =
	{
		.userdata = &app,
		.on_update = on_update,
		.on_imgui = on_imgui,
		.on_draw = on_draw,
		.width = width,
		.height = height,
		.fixed_update_time_step = 1.0 / 1,
		.render_need_interpolate = true,
		.enable_capture = false,
		.enable_profile = false,
	};
	app.device = oval_create_device(&device_descriptor);
	_init_resource(app);
	_init_world(app);
	if (app.device)
	{
		oval_runloop(app.device);
		_free_resource(app);
		oval_free_device(app.device);
	}

	return 0;
}
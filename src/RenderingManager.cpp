#include "RenderingManager.h"
#include "PipelinePrivateData.h"

using namespace Rendering;
using namespace ShaderToggler;
using namespace reshade::api;

size_t RenderingManager::g_charBufferSize = CHAR_BUFFER_SIZE;
char RenderingManager::g_charBuffer[CHAR_BUFFER_SIZE];

RenderingManager::RenderingManager(AddonImGui::AddonUIData& data, ResourceManager& rManager) : uiData(data), resourceManager(rManager)
{
}

RenderingManager::~RenderingManager()
{

}

void RenderingManager::EnumerateTechniques(effect_runtime* runtime, std::function<void(effect_runtime*, effect_technique, string&)> func)
{
    runtime->enumerate_techniques(nullptr, [func](effect_runtime* rt, effect_technique technique) {
        g_charBufferSize = CHAR_BUFFER_SIZE;
        rt->get_technique_name(technique, g_charBuffer, &g_charBufferSize);
        string name(g_charBuffer);
        func(rt, technique, name);
        });
}

void RenderingManager::_CheckCallForCommandList(ShaderData& sData, CommandListDataContainer& commandListData, const DeviceDataContainer& deviceData) const
{
    // Masks which checks to perform. Note that we will always schedule a draw call check for binding and effect updates,
    // this serves the purpose of assigning the resource_view to perform the update later on if needed.
    uint32_t queue_mask = MATCH_NONE;

    // Shift in case of VS using data id
    const uint32_t match_effect = MATCH_EFFECT_PS * sData.id;
    const uint32_t match_binding = MATCH_BINDING_PS * sData.id;
    const uint32_t match_const = MATCH_CONST_PS * sData.id;

    if (sData.blockedShaderGroups != nullptr)
    {
        for (auto group : *sData.blockedShaderGroups)
        {
            if (group->isActive())
            {
                if (group->getExtractConstants() && !deviceData.constantsUpdated.contains(group))
                {
                    if (!sData.constantBuffersToUpdate.contains(group))
                    {
                        sData.constantBuffersToUpdate.emplace(group);
                        queue_mask |= match_const;
                    }
                }

                if (group->isProvidingTextureBinding() && !deviceData.bindingsUpdated.contains(group->getTextureBindingName()))
                {
                    if (!sData.bindingsToUpdate.contains(group->getTextureBindingName()))
                    {
                        sData.bindingsToUpdate.emplace(group->getTextureBindingName(), std::make_tuple(group, group->getInvocationLocation(), resource_view{ 0 }));
                        queue_mask |= (match_binding << (group->getInvocationLocation() * MATCH_DELIMITER)) | (match_binding << CALL_DRAW * MATCH_DELIMITER);
                    }
                }

                if (group->getAllowAllTechniques())
                {
                    for (const auto& tech : deviceData.allEnabledTechniques)
                    {
                        if (group->getHasTechniqueExceptions() && group->preferredTechniques().contains(tech.first))
                        {
                            continue;
                        }

                        if (!tech.second)
                        {
                            if (!sData.techniquesToRender.contains(tech.first))
                            {
                                sData.techniquesToRender.emplace(tech.first, std::make_tuple(group, group->getInvocationLocation(), resource_view{ 0 }));
                                queue_mask |= (match_effect << (group->getInvocationLocation() * MATCH_DELIMITER)) | (match_effect << CALL_DRAW * MATCH_DELIMITER);
                            }
                        }
                    }
                }
                else if (group->preferredTechniques().size() > 0) {
                    for (auto& techName : group->preferredTechniques())
                    {
                        if (deviceData.allEnabledTechniques.contains(techName) && !deviceData.allEnabledTechniques.at(techName))
                        {
                            if (!sData.techniquesToRender.contains(techName))
                            {
                                sData.techniquesToRender.emplace(techName, std::make_tuple(group, group->getInvocationLocation(), resource_view{ 0 }));
                                queue_mask |= (match_effect << (group->getInvocationLocation() * MATCH_DELIMITER)) | (match_effect << CALL_DRAW * MATCH_DELIMITER);
                            }
                        }
                    }
                }
            }
        }
    }

    commandListData.commandQueue |= queue_mask;
}

void RenderingManager::CheckCallForCommandList(reshade::api::command_list* commandList)
{
    if (nullptr == commandList)
    {
        return;
    }

    CommandListDataContainer& commandListData = commandList->get_private_data<CommandListDataContainer>();
    DeviceDataContainer& deviceData = commandList->get_device()->get_private_data<DeviceDataContainer>();

    std::shared_lock<std::shared_mutex> r_mutex(render_mutex);
    std::shared_lock<std::shared_mutex> b_mutex(binding_mutex);

    _CheckCallForCommandList(commandListData.ps, commandListData, deviceData);
    _CheckCallForCommandList(commandListData.vs, commandListData, deviceData);

    b_mutex.unlock();
    r_mutex.unlock();
}

static inline bool IsColorBuffer(reshade::api::format value)
{
    switch (value)
    {
    default:
        return false;
    case reshade::api::format::b5g6r5_unorm:
    case reshade::api::format::b5g5r5a1_unorm:
    case reshade::api::format::b5g5r5x1_unorm:
    case reshade::api::format::r8g8b8a8_typeless:
    case reshade::api::format::r8g8b8a8_unorm:
    case reshade::api::format::r8g8b8a8_unorm_srgb:
    case reshade::api::format::r8g8b8x8_unorm:
    case reshade::api::format::r8g8b8x8_unorm_srgb:
    case reshade::api::format::b8g8r8a8_typeless:
    case reshade::api::format::b8g8r8a8_unorm:
    case reshade::api::format::b8g8r8a8_unorm_srgb:
    case reshade::api::format::b8g8r8x8_typeless:
    case reshade::api::format::b8g8r8x8_unorm:
    case reshade::api::format::b8g8r8x8_unorm_srgb:
    case reshade::api::format::r10g10b10a2_typeless:
    case reshade::api::format::r10g10b10a2_unorm:
    case reshade::api::format::r10g10b10a2_xr_bias:
    case reshade::api::format::b10g10r10a2_typeless:
    case reshade::api::format::b10g10r10a2_unorm:
    case reshade::api::format::r11g11b10_float:
    case reshade::api::format::r16g16b16a16_typeless:
    case reshade::api::format::r16g16b16a16_float:
    case reshade::api::format::r32g32b32_typeless:
    case reshade::api::format::r32g32b32_float:
    case reshade::api::format::r32g32b32a32_typeless:
    case reshade::api::format::r32g32b32a32_float:
        return true;
    }
}

const resource_view RenderingManager::GetCurrentResourceView(effect_runtime* runtime, const pair<string, tuple<const ToggleGroup*, bool, resource_view>>& matchObject, CommandListDataContainer& commandListData, uint32_t descIndex, uint32_t action)
{
    resource_view active_rtv = { 0 };
    device* device = runtime->get_device();
    const ToggleGroup* group = get<0>(matchObject.second);

    const vector<resource_view>& rtvs = commandListData.stateTracker.GetBoundRenderTargetViews();

    size_t index = group->getDescriptorIndex();
    index = std::min(index, rtvs.size() - 1);

    // Only return SRVs in case of bindings
    if(action & MATCH_BINDING && group->getExtractResourceViews())
    { 
        uint32_t slot_size = static_cast<uint32_t>(commandListData.stateTracker.GetPushDescriptorState()->current_srv[descIndex].size());
        uint32_t slot = min(group->getSRVSlotIndex(), slot_size - 1);

        if (slot_size == 0)
            return active_rtv;

        uint32_t desc_size = static_cast<uint32_t>(commandListData.stateTracker.GetPushDescriptorState()->current_srv[descIndex][slot].size());
        uint32_t desc = min(group->getSRVDescriptorIndex(), desc_size - 1);

        if (desc_size == 0)
            return active_rtv;

        resource_view buf = commandListData.stateTracker.GetPushDescriptorState()->current_srv[descIndex][slot][desc];

        active_rtv = buf;
    }
    else if (rtvs.size() > 0 && runtime != nullptr && rtvs[index] != 0)
    {
        resource rs = device->get_resource_from_view(rtvs[index]);

        if (rs == 0)
        {
            // Render targets may not have a resource bound in D3D12, in which case writes to them are discarded
            return active_rtv;
        }

        // Don't apply effects to non-RGB buffers
        resource_desc desc = device->get_resource_desc(rs);
        if (action & MATCH_EFFECT && !IsColorBuffer(desc.texture.format))
        {
            return active_rtv;
        }

        // Make sure our target matches swap buffer dimensions when applying effects or it's explicitly requested
        if (group->getMatchSwapchainResolution() || action & MATCH_EFFECT)
        {
            uint32_t width, height;
            runtime->get_screenshot_width_and_height(&width, &height);

            if (width != desc.texture.width || height != desc.texture.height)
            {
                return active_rtv;
            }
        }

        active_rtv = rtvs[index];
    }

    return active_rtv;
}

bool RenderingManager::RenderRemainingEffects(effect_runtime* runtime)
{
    if (runtime == nullptr || runtime->get_device() == nullptr)
    {
        return false;
    }

    command_list* cmd_list = runtime->get_command_queue()->get_immediate_command_list();
    device* device = runtime->get_device();
    CommandListDataContainer& commandListData = cmd_list->get_private_data<CommandListDataContainer>();
    DeviceDataContainer& deviceData = device->get_private_data<DeviceDataContainer>();
    bool rendered = false;

    resource res = runtime->get_current_back_buffer();
    resource_view active_rtv = { 0 };
    resource_view active_rtv_srgb = { 0 };

    resourceManager.SetResourceViewHandles(res.handle, &active_rtv, &active_rtv_srgb);
    
    if (deviceData.current_runtime == nullptr || active_rtv == 0 || !deviceData.rendered_effects) {
        return false;
    }
    
    EnumerateTechniques(deviceData.current_runtime, [&deviceData, &commandListData, &cmd_list, &device, &active_rtv, &active_rtv_srgb, &rendered, &res](effect_runtime* runtime, effect_technique technique, string& name) {
        if (deviceData.allEnabledTechniques.contains(name) && !deviceData.allEnabledTechniques[name])
        {
            runtime->render_technique(technique, cmd_list, active_rtv, active_rtv_srgb);
    
            deviceData.allEnabledTechniques[name] = true;
            rendered = true;
        }
        });

    return rendered;
}

bool RenderingManager::_RenderEffects(
    command_list* cmd_list,
    DeviceDataContainer& deviceData,
    const unordered_map<string, tuple<const ToggleGroup*, uint32_t, resource_view>>& techniquesToRender,
    vector<string>& removalList,
    const unordered_set<string>& toRenderNames)
{
    bool rendered = false;
    CommandListDataContainer& cmdData = cmd_list->get_private_data<CommandListDataContainer>();

    EnumerateTechniques(deviceData.current_runtime, [&cmdData, &deviceData, &techniquesToRender, &cmd_list, &rendered, &removalList, &toRenderNames, this](effect_runtime* runtime, effect_technique technique, string& name) {

        if (toRenderNames.find(name) != toRenderNames.end())
        {
            auto tech = techniquesToRender.find(name);

            if (tech != techniquesToRender.end() && !deviceData.allEnabledTechniques.at(name))
            {
                resource_view active_rtv = std::get<2>(tech->second);
                const ToggleGroup* g = std::get<0>(tech->second);

                if (active_rtv == 0)
                {
                    return;
                }

                resource res = runtime->get_device()->get_resource_from_view(active_rtv);

                resource_view view_non_srgb = active_rtv;
                resource_view view_srgb = active_rtv;

                resourceManager.SetResourceViewHandles(res.handle, &view_non_srgb, &view_srgb);

                if (view_non_srgb == 0)
                {
                    return;
                }

                deviceData.rendered_effects = true;

                runtime->render_technique(technique, cmd_list, view_non_srgb, view_srgb);

                resource_desc resDesc = runtime->get_device()->get_resource_desc(res);
                uiData.cFormat = resDesc.texture.format;
                removalList.push_back(name);

                deviceData.allEnabledTechniques[name] = true;
                rendered = true;
            }
        }
        });

    return rendered;
}

void RenderingManager::_QueueOrDequeue(
    effect_runtime* runtime,
    CommandListDataContainer& commandListData,
    std::unordered_map<std::string, std::tuple<const ShaderToggler::ToggleGroup*, uint32_t, reshade::api::resource_view>>& queue,
    unordered_set<string>& immediateQueue,
    uint32_t callLocation,
    uint32_t layoutIndex,
    uint32_t action)
{
    for (auto it = queue.begin(); it != queue.end();)
    {
        // Set views during draw call since we can be sure the correct ones are bound at that point
        if (!callLocation && std::get<2>(it->second) == 0)
        {
            resource_view active_rtv = GetCurrentResourceView(runtime, *it, commandListData, layoutIndex, action);

            if (active_rtv != 0)
            {
                std::get<2>(it->second) = active_rtv;
            }
            else if(std::get<0>(it->second)->getRequeueAfterRTMatchingFailure())
            {
                // Re-issue draw call queue command
                commandListData.commandQueue |= (action << (callLocation * MATCH_DELIMITER));
                it++;
                continue;
            }
            else
            {
                it = queue.erase(it);
                continue;
            }
        }

        // Queue updates depending on the place their supposed to be called at
        if (std::get<2>(it->second) != 0 && (!callLocation && !std::get<1>(it->second) || callLocation & std::get<1>(it->second)))
        {
            immediateQueue.insert(it->first);
        }

        it++;
    }
}

void RenderingManager::RenderEffects(command_list* cmd_list, uint32_t callLocation, uint32_t invocation)
{
    if (cmd_list == nullptr || cmd_list->get_device() == nullptr)
    {
        return;
    }

    device* device = cmd_list->get_device();
    CommandListDataContainer& commandListData = cmd_list->get_private_data<CommandListDataContainer>();
    DeviceDataContainer& deviceData = device->get_private_data<DeviceDataContainer>();

    // Remove call location from queue
    commandListData.commandQueue &= ~(invocation << (callLocation * MATCH_DELIMITER));

    if (deviceData.current_runtime == nullptr || (commandListData.ps.techniquesToRender.size() == 0 && commandListData.vs.techniquesToRender.size() == 0)) {
        return;
    }

    bool toRender = false;
    unordered_set<string> psToRenderNames;
    unordered_set<string> vsToRenderNames;

    if (invocation & MATCH_EFFECT_PS)
    {
        _QueueOrDequeue(deviceData.current_runtime, commandListData, commandListData.ps.techniquesToRender, psToRenderNames, callLocation, 0, MATCH_EFFECT_PS);
    }

    if (invocation & MATCH_EFFECT_VS)
    {
        _QueueOrDequeue(deviceData.current_runtime, commandListData, commandListData.vs.techniquesToRender, vsToRenderNames, callLocation, 1, MATCH_EFFECT_VS);
    }

    bool rendered = false;
    vector<string> psRemovalList;
    vector<string> vsRemovalList;

    if (psToRenderNames.size() == 0 && vsToRenderNames.size() == 0)
    {
        return;
    }

    deviceData.current_runtime->render_effects(cmd_list, resource_view{ 0 }, resource_view{ 0 });

    std::unique_lock<shared_mutex> dev_mutex(render_mutex);
    rendered = (psToRenderNames.size() > 0) && _RenderEffects(cmd_list, deviceData, commandListData.ps.techniquesToRender, psRemovalList, psToRenderNames) ||
        (vsToRenderNames.size() > 0) && _RenderEffects(cmd_list, deviceData, commandListData.vs.techniquesToRender, vsRemovalList, vsToRenderNames);
    dev_mutex.unlock();

    for (auto& g : psRemovalList)
    {
        commandListData.ps.techniquesToRender.erase(g);
    }

    for (auto& g : vsRemovalList)
    {
        commandListData.vs.techniquesToRender.erase(g);
    }

    if (rendered)
    {
        // TODO: ???
        //std::shared_lock<std::shared_mutex> dev_mutex(pipeline_layout_mutex);
        commandListData.stateTracker.ReApplyState(cmd_list, deviceData.transient_mask);
    }
}

void RenderingManager::InitTextureBingings(effect_runtime* runtime)
{
    DeviceDataContainer& data = runtime->get_device()->get_private_data<DeviceDataContainer>();

    // Init empty texture
    CreateTextureBinding(runtime, &empty_res, &empty_srv, &empty_rtv, reshade::api::format::r8g8b8a8_unorm);

    // Initialize texture bindings with default format
    for (auto& group : uiData.GetToggleGroups())
    {
        if (group.second.isProvidingTextureBinding() && group.second.getTextureBindingName().length() > 0)
        {
            resource res = { 0 };
            resource_view srv = { 0 };
            resource_view rtv = { 0 };

            std::unique_lock<shared_mutex> lock(binding_mutex);
            if (group.second.getCopyTextureBinding() && CreateTextureBinding(runtime, &res, &srv, &rtv, reshade::api::format::r8g8b8a8_unorm))
            {
                data.bindingMap[group.second.getTextureBindingName()] = TextureBindingData{ res, reshade::api::format::r8g8b8a8_unorm, rtv, srv, 0, 0, group.second.getClearBindings(), group.second.getCopyTextureBinding(), false };
                runtime->update_texture_bindings(group.second.getTextureBindingName().c_str(), srv);
            }
            else if (!group.second.getCopyTextureBinding())
            {
                data.bindingMap[group.second.getTextureBindingName()] = TextureBindingData{ resource { 0 }, format::unknown, resource_view { 0 }, resource_view { 0 }, 0, 0, group.second.getClearBindings(), group.second.getCopyTextureBinding(), false};
                runtime->update_texture_bindings(group.second.getTextureBindingName().c_str(), resource_view{ 0 }, resource_view{ 0 });
            }
        }
    }
}

void RenderingManager::DisposeTextureBindings(effect_runtime* runtime)
{
    DeviceDataContainer& data = runtime->get_device()->get_private_data<DeviceDataContainer>();

    std::unique_lock<shared_mutex> lock(binding_mutex);

    if (empty_res != 0)
    {
        runtime->get_device()->destroy_resource(empty_res);
    }

    if (empty_srv != 0)
    {
        runtime->get_device()->destroy_resource_view(empty_srv);
    }

    if (empty_rtv != 0)
    {
        runtime->get_device()->destroy_resource_view(empty_rtv);
    }

    for (auto& binding : data.bindingMap)
    {
        DestroyTextureBinding(runtime, binding.first);
    }

    data.bindingMap.clear();
}

bool RenderingManager::_CreateTextureBinding(reshade::api::effect_runtime* runtime,
    reshade::api::resource* res,
    reshade::api::resource_view* srv,
    reshade::api::resource_view* rtv,
    reshade::api::format format,
    uint32_t width,
    uint32_t height)
{
    runtime->get_command_queue()->wait_idle();

    if (!runtime->get_device()->create_resource(
        resource_desc(width, height, 1, 1, format_to_typeless(format), 1, memory_heap::gpu_only, resource_usage::copy_dest | resource_usage::shader_resource | resource_usage::render_target),
        nullptr, resource_usage::shader_resource, res))
    {
        reshade::log_message(reshade::log_level::error, "Failed to create texture binding resource!");
        return false;
    }

    if (!runtime->get_device()->create_resource_view(*res, resource_usage::shader_resource, resource_view_desc(format_to_default_typed(format, 0)), srv))
    {
        reshade::log_message(reshade::log_level::error, "Failed to create texture binding resource view!");
        return false;
    }

    if (!runtime->get_device()->create_resource_view(*res, resource_usage::render_target, resource_view_desc(format_to_default_typed(format, 0)), rtv))
    {
        reshade::log_message(reshade::log_level::error, "Failed to create texture binding resource view!");
        return false;
    }

    return true;
}

bool RenderingManager::CreateTextureBinding(effect_runtime* runtime, resource* res, resource_view* srv, resource_view* rtv, const resource_desc& desc)
{
    reshade::api::format format = desc.texture.format;

    uint32_t frame_width, frame_height;
    frame_height = desc.texture.height;
    frame_width = desc.texture.width;

    return _CreateTextureBinding(runtime, res, srv, rtv, format, frame_width, frame_height);
}

bool RenderingManager::CreateTextureBinding(effect_runtime* runtime, resource* res, resource_view* srv, resource_view* rtv, reshade::api::format format)
{
    uint32_t frame_width, frame_height;
    runtime->get_screenshot_width_and_height(&frame_width, &frame_height);

    return _CreateTextureBinding(runtime, res, srv, rtv, format, frame_width, frame_height);
}

void RenderingManager::DestroyTextureBinding(effect_runtime* runtime, const string& binding)
{
    DeviceDataContainer& data = runtime->get_device()->get_private_data<DeviceDataContainer>();

    auto it = data.bindingMap.find(binding);

    if (it != data.bindingMap.end())
    {
        // Destroy copy resource if copy option is enabled, otherwise just reset the binding
        if (it->second.copy)
        {
            resource res = { 0 };
            resource_view srv = { 0 };
            resource_view rtv = { 0 };

            runtime->get_command_queue()->wait_idle();

            res = it->second.res;
            if (res != 0)
            {
                runtime->get_device()->destroy_resource(res);
            }

            srv = it->second.srv;
            if (srv != 0)
            {
                runtime->get_device()->destroy_resource_view(srv);
            }

            rtv = it->second.rtv;
            if (rtv != 0)
            {
                runtime->get_device()->destroy_resource_view(rtv);
            }
        }

        runtime->update_texture_bindings(binding.c_str(), resource_view{ 0 }, resource_view{ 0 });

        it->second.res = { 0 };
        it->second.rtv = { 0 };
        it->second.srv = { 0 };
    }
}


uint32_t RenderingManager::UpdateTextureBinding(effect_runtime* runtime, const string& binding, const resource_desc& desc)
{
    DeviceDataContainer& data = runtime->get_device()->get_private_data<DeviceDataContainer>();

    auto it = data.bindingMap.find(binding);

    if (it != data.bindingMap.end())
    {
        reshade::api::format oldFormat = it->second.format;
        reshade::api::format format = desc.texture.format;
        uint32_t oldWidth = it->second.width;
        uint32_t width = desc.texture.width;
        uint32_t oldHeight = it->second.height;
        uint32_t height = desc.texture.height;

        if (format != oldFormat || oldWidth != width || oldHeight != height)
        {
            DestroyTextureBinding(runtime, binding);

            resource res = {};
            resource_view srv = {};
            resource_view rtv = {};

            if (CreateTextureBinding(runtime, &res, &srv, &rtv, desc))
            {
                it->second.res = res;
                it->second.srv = srv;
                it->second.rtv = rtv;
                runtime->update_texture_bindings(binding.c_str(), srv);
            }
            else
            {
                return 0;
            }

            return 2;
        }
    }
    else
    {
        return 0;
    }

    return 1;
}


void RenderingManager::_UpdateTextureBindings(command_list* cmd_list,
    DeviceDataContainer& deviceData,
    const unordered_map<string, tuple<const ToggleGroup*, uint32_t, resource_view>>& bindingsToUpdate,
    vector<string>& removalList,
    const unordered_set<string>& toUpdateBindings)
{
    for (auto& binding : bindingsToUpdate)
    {
        if (toUpdateBindings.contains(binding.first) && !deviceData.bindingsUpdated.contains(binding.first))
        {
            string bindingName = binding.first;
            effect_runtime* runtime = deviceData.current_runtime;

            resource_view active_rtv = std::get<2>(binding.second);

            if (active_rtv == 0)
            {
                continue;
            }

            auto it = deviceData.bindingMap.find(bindingName);

            if (it != deviceData.bindingMap.end())
            {
                resource res = runtime->get_device()->get_resource_from_view(active_rtv);

                if (res == 0)
                {
                    continue;
                }

                if (!it->second.copy)
                {
                    resource_view view_non_srgb = { 0 };
                    resource_view view_srgb = { 0 };

                    resourceManager.SetShaderResourceViewHandles(res.handle, &view_non_srgb, &view_srgb);

                    if (view_non_srgb == 0)
                    {
                        return;
                    }

                    resource_desc resDesc = runtime->get_device()->get_resource_desc(res);

                    resource target_res = it->second.res;

                    if (target_res != res)
                    {
                        runtime->update_texture_bindings(bindingName.c_str(), view_non_srgb, view_srgb);

                        it->second.res = res;
                        it->second.format = resDesc.texture.format;
                        it->second.srv = { 0 };
                        it->second.rtv = { 0 };
                        it->second.width = 0;
                        it->second.height = 0;
                    }

                    it->second.reset = false;
                }
                else
                {
                    resource_desc resDesc = runtime->get_device()->get_resource_desc(res);

                    uint32_t retUpdate = UpdateTextureBinding(runtime, bindingName, resDesc);

                    resource target_res = it->second.res;

                    if (retUpdate && target_res != 0)
                    {
                        cmd_list->copy_resource(res, target_res);
                        it->second.reset = false;
                    }
                }

                deviceData.bindingsUpdated.emplace(bindingName);
                removalList.push_back(bindingName);
            }
        }
    }
}

void RenderingManager::UpdateTextureBindings(command_list* cmd_list, uint32_t callLocation, uint32_t invocation)
{
    if (cmd_list == nullptr || cmd_list->get_device() == nullptr)
    {
        return;
    }

    device* device = cmd_list->get_device();
    CommandListDataContainer& commandListData = cmd_list->get_private_data<CommandListDataContainer>();
    DeviceDataContainer& deviceData = device->get_private_data<DeviceDataContainer>();

    // Remove call location from queue
    commandListData.commandQueue &= ~(invocation << (callLocation * MATCH_DELIMITER));

    if (deviceData.current_runtime == nullptr || (commandListData.ps.bindingsToUpdate.size() == 0 && commandListData.vs.bindingsToUpdate.size() == 0)) {
        return;
    }

    unordered_set<string> psToUpdateBindings;
    unordered_set<string> vsToUpdateBindings;

    if (invocation & MATCH_BINDING_PS)
    {
        _QueueOrDequeue(deviceData.current_runtime, commandListData, commandListData.ps.bindingsToUpdate, psToUpdateBindings, callLocation, 0, MATCH_BINDING_PS);
    }

    if (invocation & MATCH_BINDING_VS)
    {
        _QueueOrDequeue(deviceData.current_runtime, commandListData, commandListData.vs.bindingsToUpdate, vsToUpdateBindings, callLocation, 1, MATCH_BINDING_VS);
    }

    if (psToUpdateBindings.size() == 0 && vsToUpdateBindings.size() == 0)
    {
        return;
    }

    vector<string> psRemovalList;
    vector<string> vsRemovalList;

    std::unique_lock<shared_mutex> mtx(binding_mutex);
    if (psToUpdateBindings.size() > 0)
    {
        _UpdateTextureBindings(cmd_list, deviceData, commandListData.ps.bindingsToUpdate, psRemovalList, psToUpdateBindings);
    }
    if (vsToUpdateBindings.size() > 0)
    {
        _UpdateTextureBindings(cmd_list, deviceData, commandListData.vs.bindingsToUpdate, vsRemovalList, vsToUpdateBindings);
    }
    mtx.unlock();

    for (auto& g : psRemovalList)
    {
        commandListData.ps.bindingsToUpdate.erase(g);
    }

    for (auto& g : vsRemovalList)
    {
        commandListData.vs.bindingsToUpdate.erase(g);
    }
}

void RenderingManager::ClearUnmatchedTextureBindings(reshade::api::command_list* cmd_list)
{
    DeviceDataContainer& data = cmd_list->get_device()->get_private_data<DeviceDataContainer>();

    std::shared_lock<shared_mutex> mtx(binding_mutex);
    if (data.bindingMap.size() == 0)
    {
        return;
    }

    static const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    for (auto& binding : data.bindingMap)
    {
        if (data.bindingsUpdated.contains(binding.first) || !binding.second.enabled_reset_on_miss || binding.second.reset)
        {
            continue;
        }

        if (!binding.second.copy)
        {
            data.current_runtime->update_texture_bindings(binding.first.c_str(), empty_srv);

            binding.second.res = { 0 };
            binding.second.srv = { 0 };
            binding.second.rtv = { 0 };
            binding.second.width = 0;
            binding.second.height = 0;
        }
        else
        {
            resource_view rtv = binding.second.rtv;

            if (rtv != 0)
            {
                cmd_list->clear_render_target_view(rtv, clearColor);
            }
        }

        binding.second.reset = true;
    }
}

void RenderingManager::ClearQueue2(CommandListDataContainer& commandListData, const uint32_t location0, const uint32_t location1) const
{
    if (commandListData.commandQueue & ((Rendering::MATCH_ALL << location0 * Rendering::MATCH_DELIMITER) | (Rendering::MATCH_ALL << location1 * Rendering::MATCH_DELIMITER)))
    {
        commandListData.commandQueue &= ~(Rendering::MATCH_ALL << location0 * Rendering::MATCH_DELIMITER);
        commandListData.commandQueue &= ~(Rendering::MATCH_ALL << location1 * Rendering::MATCH_DELIMITER);

        if (commandListData.ps.techniquesToRender.size() > 0)
        {
            for (auto it = commandListData.ps.techniquesToRender.begin(); it != commandListData.ps.techniquesToRender.end();)
            {
                uint32_t callLocation = std::get<1>(it->second);
                if (callLocation == location0 || callLocation == location1)
                {
                    it = commandListData.ps.techniquesToRender.erase(it);
                    continue;
                }
                it++;
            }
        }

        if (commandListData.vs.techniquesToRender.size() > 0)
        {
            for (auto it = commandListData.vs.techniquesToRender.begin(); it != commandListData.vs.techniquesToRender.end();)
            {
                uint32_t callLocation = std::get<1>(it->second);
                if (callLocation == location0 || callLocation == location1)
                {
                    it = commandListData.vs.techniquesToRender.erase(it);
                    continue;
                }
                it++;
            }
        }
    }
}
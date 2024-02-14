#pragma once
#include "stdafx.h"
#include "Resource.h"
#include "SceneObject.h"
#include <set>

class Camera;
class SceneObject;
class Portal;

class Portal : SceneObject
{
public:
	Portal(ID3D12Device* device, const D3D12_CPU_DESCRIPTOR_HANDLE rtvCpuDescriptorHandle, std::shared_ptr<Primitive> model, std::shared_ptr<ShaderResourceView> texture, std::shared_ptr<ConstantBufferView> constantBuffer, std::string name);

	void SetOtherPortal(std::shared_ptr<Portal> otherPortal)
	{
		m_otherPortal = otherPortal;
	}
	
	void Draw(ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle, std::set<std::shared_ptr<SceneObject>>& objects);
	
private:
	/** 
	* The portal's 'other side', whose camera will be used to render this portal.
	*/
	std::shared_ptr<Portal> m_otherPortal;
	/** 
	* The portal's camera which is used to determine the mvp matrix when drawing the portal
	*/
	std::unique_ptr<Camera> m_camera;

	const D3D12_CPU_DESCRIPTOR_HANDLE m_rtvCpuDescriptorHandle;
};


#include "Portal.h"

#include "Camera.h";
#include "RenderTexture.h"

using namespace DirectX;

Portal::Portal(std::shared_ptr<Primitive> model, std::shared_ptr<RenderTexture> renderTexture, std::shared_ptr<ConstantBufferView> constantBuffer, std::string name, std::set<std::shared_ptr<SceneObject>>& objects, std::shared_ptr<Camera>& camera)
	: SceneObject(model, renderTexture, constantBuffer, name)	// Store an existing scene object as opposed to creating it. This allows scene objects to be drawn independently of the portal's render texture etc.
	, m_renderTexture(renderTexture)
	, g_objects(objects)
	, g_camera(camera)
{
	m_camera = std::make_shared<Camera>(m_position, m_forward, XMFLOAT3(0.0f, 1.0f, 0.0f), m_scale.x / m_scale.y);	// Create the camera from the scene objects position
	SetRotation(m_rotation);	// Update the forward vector according to orientation
}

void Portal::DrawTexture(ID3D12GraphicsCommandList* commandList)
{
	m_renderTexture->BeginDraw(commandList);
	if (m_otherPortal)
	{
		auto targetRotation = m_otherPortal->GetRotation();
		auto cameraPos = g_camera->GetPosition();

		auto cameraToThisV = XMLoadFloat3(&m_position) - XMLoadFloat3(&cameraPos);
		// Find the difference in rotation between this portal and the target
		auto qDifferenceRotationV = XMQuaternionRotationRollPitchYawFromVector(XMLoadFloat3(&targetRotation) - XMLoadFloat3(&m_rotation));
		// rotate Camera->This to put it in respect to This' forward
		cameraToThisV = XMVector3Rotate(cameraToThisV, qDifferenceRotationV);

		XMFLOAT3 targetDirection;
		XMStoreFloat3(&targetDirection, cameraToThisV);

		m_otherPortal->m_camera->SetDirection(targetDirection);

		for (auto object : g_objects)
		{
			if (object->GetName() != m_name)
			{
				object->UpdateConstantBuffer(m_otherPortal->GetView(), m_otherPortal->GetProj());
				object->Draw(commandList);
			}
		}
	}
	m_renderTexture->EndDraw(commandList);
}

void Portal::SetPosition(const DirectX::XMFLOAT3& position)
{
	SceneObject::SetPosition(position);
	m_camera->SetPosition(position);
}

void Portal::SetRotation(const DirectX::XMFLOAT3& rotation)
{
	SceneObject::SetRotation(rotation);
	// TODO : Update camera direction according to rotation
	// To get the direction the camera should be in:
	// * find the direction from the player camera to this portal?
	// * this will give the illusion of the portal being a seamless part of the world

	
	//m_camera->SetDirection(m_forward);

}

void Portal::SetScale(const DirectX::XMFLOAT3& scale)
{
	SceneObject::SetScale(scale);
	m_camera->SetAspectRatio(scale.x / scale.y);
}


const DirectX::XMMATRIX Portal::GetView()
{
	return m_camera->GetView();
}

const DirectX::XMMATRIX Portal::GetProj()
{
	return m_camera->GetProj();
}

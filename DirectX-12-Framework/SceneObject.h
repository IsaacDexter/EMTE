#pragma once
#include "stdafx.h"
#include <DirectXCollision.h>
#include <string>

class Primitive;
struct Resource;
struct ConstantBufferView;

class SceneObject
{
public:
	SceneObject(std::shared_ptr<Primitive> model, std::shared_ptr<Resource> texture, std::shared_ptr<ConstantBufferView> constantBuffer, std::string name);
	virtual void Initialize() {};
	virtual void Draw(ID3D12GraphicsCommandList* commandList);
	virtual void Update(const double deltaTime) {};
	void UpdateConstantBuffer(const DirectX::XMMATRIX view, const DirectX::XMMATRIX projection);

	const DirectX::XMFLOAT3& GetPosition() const
	{
		return m_position;
	}

	virtual void SetPosition(const DirectX::XMFLOAT3& position)
	{
		m_position = position;
		m_boundingBox.Center = position;
	}

	const DirectX::XMFLOAT3& GetRotation() const
	{
		return m_rotation;
	}

	virtual void SetRotation(const DirectX::XMFLOAT3& rotation);
	

	const DirectX::XMFLOAT3& GetScale() const
	{
		return m_scale;
	}

	virtual void SetScale(const DirectX::XMFLOAT3& scale)
	{
		m_scale = scale;
		m_boundingBox.Extents.x = scale.x / 2.0f;
		m_boundingBox.Extents.y = scale.y / 2.0f;
		m_boundingBox.Extents.z = scale.z / 2.0f;
	}
	

	const DirectX::XMMATRIX GetWorld() const;
	

	std::shared_ptr<Primitive> GetModel()
	{
		return m_model;
	}

	void SetModel(std::shared_ptr<Primitive> model)
	{
		m_model = model;
	}

	std::shared_ptr<Resource> GetTexture()
	{
		return m_texture;
	}

	void SetTexture(std::shared_ptr<Resource> texture)
	{
		m_texture = texture;
	}

	DirectX::BoundingOrientedBox& GetBoundingBox()
	{
		return m_boundingBox;
	}

	const std::string& GetName()
	{
		return m_name;
	}

	void SetName(const std::string& name)
	{
		m_name = name;
	}

	const DirectX::XMFLOAT3 GetForward()
	{
		return m_forward;
	}
protected:
	DirectX::XMFLOAT3 m_position;
	DirectX::XMFLOAT3 m_rotation;
	DirectX::XMFLOAT3 m_scale;
	DirectX::XMFLOAT3 m_forward;

	std::string m_name;

	DirectX::BoundingOrientedBox m_boundingBox;

	std::shared_ptr<Primitive> m_model;
	std::shared_ptr<Resource> m_texture;
	std::shared_ptr<ConstantBufferView> m_constantBuffer;

};


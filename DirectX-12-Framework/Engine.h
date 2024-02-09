#pragma once
#include "stdafx.h"
#include "SceneObject.h"
#include <set>
#include "Camera.h"
#include <string>
#include "Window.h"
#include "Renderer.h"

class Engine
{
public:
	Engine(HINSTANCE hInstance);
	void Initialize();
	void Update();
	void Render();
	void OnKeyDown(WPARAM wParam);
	void OnKeyUp(WPARAM wParam);
	void OnMouseMove(int x, int y, WPARAM wParam);
	void OnResize();
protected:
	std::set<std::shared_ptr<SceneObject>> m_sceneObjects;
	std::unique_ptr<Camera> m_camera;

	std::unique_ptr<Renderer> m_renderer;
	std::unique_ptr<Window> m_window;

	const Microsoft::WRL::ComPtr<ID3D12Device> m_device;

	UINT m_numObjects = 20;

	void CreateObject();

	std::shared_ptr<SceneObject> m_selectedObject = nullptr;

	DirectX::XMFLOAT3 CreateRay(int x, int y);
	std::shared_ptr<SceneObject> Pick(const DirectX::XMFLOAT3& rayOrigin, const DirectX::XMFLOAT3& rayDirection);
};

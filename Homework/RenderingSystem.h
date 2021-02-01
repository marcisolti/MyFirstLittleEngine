#pragma once

#include <Egg/Common.h>
#include <Egg/Shader.h>
#include <Egg/Math/Math.h>
using namespace Egg::Math;
#include <Egg/Cam/FirstPerson.h>

#include "DescriptorHeap.h"
#include "GPSO.h"
#include "Geometry.h"
#include "Tex2D.h"
#include "ConstantBuffer.hpp"

#include <map>
#include <vector>

#include "PhysicsSystem.h"

// struct for constant buffer uploading
struct Light
{
	Float4 position, color;
};

__declspec(align(256)) struct PerFrameCb {
	Float4x4 viewProjTransform;
	Float4x4 rayDirTransform;
	Float4 eyePos;
	Light lights[64];
	int nrLights;
};

class RenderingSystem
{
	// a big heap for everyone :3
	GG::DescriptorHeap::P heap;

	// camera + lights CBV
	GG::ConstantBuffer<PerFrameCb> perFrameCb;	

	// main rendering resources
	com_ptr<ID3D12RootSignature> rootSig;
	GG::GPSO::P gpso;

	std::map<std::string, GG::Geometry::P> geometries;
	std::map<std::string, GG::Tex2D::P> textures;
	uint32_t textureCount = 0;

	// light (as a mesh) drawing resources
	std::map<std::string, Float3> lights; // actually storing just the color (intensity) here
	com_ptr<ID3D12RootSignature> lightRootSig;
	GG::GPSO::P lightGpso;
	GG::Geometry::P lightGeo;

public:

	Egg::Cam::FirstPerson::P camera;

	RenderingSystem() {}

	// load/create resources
	void StartUp(ID3D12Device* device)
	{

		heap = GG::DescriptorHeap::Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2048, true);

		{
			com_ptr<ID3DBlob> vs = Egg::Shader::LoadCso("Shaders/pbrVS.cso");
			com_ptr<ID3DBlob> ps = Egg::Shader::LoadCso("Shaders/pbrPS.cso");
			rootSig = Egg::Shader::LoadRootSignature(device, vs.Get());

			gpso = GG::GPSO::Create(device, rootSig.Get(), vs.Get(), ps.Get());
		}

		{
			com_ptr<ID3DBlob> vs = Egg::Shader::LoadCso("Shaders/lightVS.cso");
			com_ptr<ID3DBlob> ps = Egg::Shader::LoadCso("Shaders/lightPS.cso");
			lightRootSig = Egg::Shader::LoadRootSignature(device, vs.Get());

			lightGpso = GG::GPSO::Create(device, lightRootSig.Get(), vs.Get(), ps.Get());

			lightGeo = GG::Geometry::Create(device, "ball_low.obj");
		}

		camera = Egg::Cam::FirstPerson::Create()->SetView(Float3(0, 5, -7), Float3(0, 0, 1));
		perFrameCb.CreateResources(device, sizeof(PerFrameCb));

	}

	void UploadTextures(ID3D12GraphicsCommandList* commandList)
	{
		for (const auto& tex : textures)
			tex.second->UploadResources(commandList);
	}

	void Update(PxSystem* physics, float dt)
	{

		// perFrameCb
		{
			camera->Animate(dt);
			perFrameCb->viewProjTransform = camera->GetViewMatrix() * camera->GetProjMatrix();
			perFrameCb->rayDirTransform = camera->GetRayDirMatrix();
			perFrameCb->eyePos.xyz = camera->GetEyePosition();
			perFrameCb->eyePos.w = 1.0f;
			
			int i = 0;
			for (const auto& [id, light] : lights) 
			{
				perFrameCb->lights[i].position = Float4{ physics->GetRigidBody(id)->GetPosition(), 1 };
				perFrameCb->lights[i].color    = Float4{ light, 1 };
				i++;
			}
			perFrameCb->nrLights = i;
			
			perFrameCb.Upload();
		}

	}

	void Draw(ID3D12GraphicsCommandList* commandList, PxSystem* physics)
	{
		heap->BindHeap(commandList);

		// sort of render passes ?
		commandList->SetGraphicsRootSignature(rootSig.Get());
		commandList->SetPipelineState(gpso->Get());
		commandList->SetGraphicsRootConstantBufferView(0, perFrameCb.GetGPUVirtualAddress());

		for (const auto& [id, geometry] : geometries)
		{

			physics->BindConstantBuffer(commandList, id);

			commandList->SetGraphicsRootDescriptorTable(
				2, 
				heap->GetGPUHandle(textures[id]->index)
			);
			
			geometry->Draw(commandList);

		}

		// draw lights
		{
			commandList->SetGraphicsRootSignature(lightRootSig.Get());
			commandList->SetPipelineState(lightGpso->Get());
			commandList->SetGraphicsRootConstantBufferView(0, perFrameCb.GetGPUVirtualAddress());

			for (const auto& [id, light] : lights)
			{
				physics->BindConstantBuffer(commandList, id);
				lightGeo->Draw(commandList);
			}

		}

	}

	void AddShadedMesh(
		ID3D12Device* device,
		const std::string& id,
		const std::string& meshPath,
		const std::string& texPath
	) {

		// check if geo isn't already imported (not pretty)
		GG::Geometry::P newGeo{ nullptr };
		{
			
			for (const auto& [id, geo] : geometries)
			{
				if (geo->path == meshPath)
					newGeo = geo;
			}
		
			if(!newGeo)
				newGeo = GG::Geometry::Create(device, meshPath);

		}
		geometries.insert({ id, newGeo });

		// same for texture
		GG::Tex2D::P newTex{ nullptr };
		{
			
			for (const auto& [id, tex] : textures)
			{
				if (tex->path == texPath)
					newTex = tex;
			}

			if (!newTex)
			{
				newTex = GG::Tex2D::Create(device, heap, texPath);
				newTex->CreateSrv(device, heap, textureCount);
				textureCount++;
			}

		}
		textures.insert({ id, newTex });
	}

	void AddLight(
		const std::string& id,
		Float3 color
	) {
		lights.insert({ id, color });
	}

	void ProcessMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) 
	{ 
		camera->ProcessMessage(hWnd, uMsg, wParam, lParam); 
	}

};
// Copyright (c) 2025 zolnoor. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actions/EditorAction.h"

// Forward declarations
class UNiagaraSystem;
class UNiagaraEmitter;

// ============================================================================
// P8: Niagara Particle System Actions
// ============================================================================

/**
 * FNiagaraAction — Base class for Niagara actions with common helpers.
 */
class UEEDITORMCP_API FNiagaraAction : public FEditorAction
{
protected:
	UNiagaraSystem* FindNiagaraSystem(const FString& SystemName, FString& OutError) const;
};


/** Create a new Niagara System asset. */
class UEEDITORMCP_API FCreateNiagaraSystemAction : public FNiagaraAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("create_niagara_system"); }
};

/** Describe a Niagara System structure (emitters, modules, renderers). */
class UEEDITORMCP_API FDescribeNiagaraSystemAction : public FNiagaraAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("describe_niagara_system"); }
	virtual bool RequiresSave() const override { return false; }
};

/** Add an emitter to a Niagara System. */
class UEEDITORMCP_API FAddNiagaraEmitterAction : public FNiagaraAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("add_niagara_emitter"); }
};

/** Remove an emitter from a Niagara System. */
class UEEDITORMCP_API FRemoveNiagaraEmitterAction : public FNiagaraAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("remove_niagara_emitter"); }
};

/** Set a module parameter on a Niagara Emitter. */
class UEEDITORMCP_API FSetNiagaraModuleParamAction : public FNiagaraAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("set_niagara_module_param"); }
};

/** Compile a Niagara System and return diagnostics. */
class UEEDITORMCP_API FCompileNiagaraSystemAction : public FNiagaraAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("compile_niagara_system"); }
};

/** List available Niagara module/emitter templates. */
class UEEDITORMCP_API FGetNiagaraModulesAction : public FNiagaraAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override { return true; }
	virtual FString GetActionName() const override { return TEXT("get_niagara_modules"); }
	virtual bool RequiresSave() const override { return false; }
};

// Copyright (c) 2025 zolnoor. All rights reserved.

#include "Actions/PythonActions.h"
#include "IPythonScriptPlugin.h"
#include "Misc/ScopeLock.h"

// =========================================================================
// FExecPythonAction
// =========================================================================

bool FExecPythonAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Code;
	if (!GetRequiredString(Params, TEXT("code"), Code, OutError))
	{
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FExecPythonAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	// Check PythonScriptPlugin availability
	IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
	if (!PythonPlugin || !PythonPlugin->IsPythonAvailable())
	{
		return CreateErrorResponse(
			TEXT("PythonScriptPlugin is not available. Enable it in .uproject."),
			TEXT("python_unavailable")
		);
	}

	FString Code = Params->GetStringField(TEXT("code"));

	// Build a wrapper script that:
	// 1. Redirects stdout/stderr to capture them
	// 2. Executes the user code
	// 3. Serializes _result to JSON
	// 4. Stores everything in __mcp_* variables for retrieval
	FString WrapperCode = FString::Printf(TEXT(
		"import sys as __mcp_sys\n"
		"import io as __mcp_io\n"
		"import json as __mcp_json\n"
		"import traceback as __mcp_traceback\n"
		"\n"
		"__mcp_stdout_capture = __mcp_io.StringIO()\n"
		"__mcp_stderr_capture = __mcp_io.StringIO()\n"
		"__mcp_old_stdout = __mcp_sys.stdout\n"
		"__mcp_old_stderr = __mcp_sys.stderr\n"
		"__mcp_sys.stdout = __mcp_stdout_capture\n"
		"__mcp_sys.stderr = __mcp_stderr_capture\n"
		"__mcp_success = True\n"
		"__mcp_error = ''\n"
		"__mcp_return_value = None\n"
		"__mcp_return_value_json = 'null'\n"
		"\n"
		"try:\n"
		"    exec(r\"\"\"%s\"\"\")\n"
		"    if '_result' in dir():\n"
		"        __mcp_return_value = _result\n"
		"        try:\n"
		"            __mcp_return_value_json = __mcp_json.dumps(__mcp_return_value, default=str, ensure_ascii=False)\n"
		"        except Exception as __mcp_e:\n"
		"            __mcp_return_value_json = __mcp_json.dumps(str(__mcp_return_value))\n"
		"except Exception as __mcp_e:\n"
		"    __mcp_success = False\n"
		"    __mcp_error = str(__mcp_e)\n"
		"    __mcp_stderr_capture.write(__mcp_traceback.format_exc())\n"
		"finally:\n"
		"    __mcp_sys.stdout = __mcp_old_stdout\n"
		"    __mcp_sys.stderr = __mcp_old_stderr\n"
		"    __mcp_stdout_str = __mcp_stdout_capture.getvalue()\n"
		"    __mcp_stderr_str = __mcp_stderr_capture.getvalue()\n"
	), *EscapePythonString(Code));

	// Execute the wrapper
	bool bExecSuccess = PythonPlugin->ExecPythonCommand(*WrapperCode);

	// Now retrieve results by executing small getter scripts
	FString StdoutStr, StderrStr, ReturnValueJson, ErrorStr;
	bool bPythonSuccess = true;

	// Get stdout
	{
		FString GetStdout = TEXT(
			"import sys as __mcp_sys\n"
			"__mcp_sys.stdout.write('__MCP_OUT__' + __mcp_stdout_str + '__MCP_END__')\n"
		);
		// Use a simpler approach: query variables via ExecPythonCommand
	}

	// Simpler approach: use a single retrieval script that prints a JSON envelope
	FString RetrieveCode = TEXT(
		"import json as __mcp_json2, sys as __mcp_sys2\n"
		"__mcp_envelope = __mcp_json2.dumps({\n"
		"    'success': __mcp_success,\n"
		"    'error': __mcp_error,\n"
		"    'stdout': __mcp_stdout_str,\n"
		"    'stderr': __mcp_stderr_str,\n"
		"    'return_value_json': __mcp_return_value_json\n"
		"}, ensure_ascii=False)\n"
		"# Write to a temp file so C++ can read it\n"
		"import tempfile as __mcp_tempfile, os as __mcp_os\n"
		"__mcp_result_path = __mcp_os.path.join(__mcp_tempfile.gettempdir(), '__mcp_python_result.json')\n"
		"with open(__mcp_result_path, 'w', encoding='utf-8') as __mcp_f:\n"
		"    __mcp_f.write(__mcp_envelope)\n"
	);
	PythonPlugin->ExecPythonCommand(*RetrieveCode);

	// Read the result file
	FString TempDir = FPlatformProcess::UserTempDir();
	FString ResultFilePath = FPaths::Combine(TempDir, TEXT("__mcp_python_result.json"));
	FString ResultJson;

	if (!FFileHelper::LoadFileToString(ResultJson, *ResultFilePath))
	{
		return CreateErrorResponse(
			TEXT("Failed to read Python execution result. The code may have crashed the Python interpreter."),
			TEXT("python_result_read_error")
		);
	}

	// Clean up temp file
	IFileManager::Get().Delete(*ResultFilePath);

	// Parse the envelope JSON
	TSharedPtr<FJsonObject> Envelope;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResultJson);
	if (!FJsonSerializer::Deserialize(Reader, Envelope) || !Envelope.IsValid())
	{
		return CreateErrorResponse(
			TEXT("Failed to parse Python execution result JSON."),
			TEXT("python_result_parse_error")
		);
	}

	bool bSuccess = Envelope->GetBoolField(TEXT("success"));
	StdoutStr = Envelope->GetStringField(TEXT("stdout"));
	StderrStr = Envelope->GetStringField(TEXT("stderr"));
	ErrorStr = Envelope->GetStringField(TEXT("error"));
	ReturnValueJson = Envelope->GetStringField(TEXT("return_value_json"));

	if (!bSuccess)
	{
		TSharedPtr<FJsonObject> ErrorResult = MakeShared<FJsonObject>();
		ErrorResult->SetStringField(TEXT("stderr"), StderrStr);
		ErrorResult->SetStringField(TEXT("stdout"), StdoutStr);

		TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
		Response->SetStringField(TEXT("status"), TEXT("error"));
		Response->SetBoolField(TEXT("success"), false);
		Response->SetStringField(TEXT("error"), ErrorStr);
		Response->SetStringField(TEXT("error_type"), TEXT("python_exception"));
		Response->SetStringField(TEXT("stderr"), StderrStr);
		return Response;
	}

	// Parse return_value from its JSON string
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("stdout"), StdoutStr);
	ResultData->SetStringField(TEXT("stderr"), StderrStr);

	// Parse return_value_json into a proper JSON value
	TSharedPtr<FJsonValue> ReturnValue;
	TSharedRef<TJsonReader<>> ReturnReader = TJsonReaderFactory<>::Create(ReturnValueJson);
	if (FJsonSerializer::Deserialize(ReturnReader, ReturnValue) && ReturnValue.IsValid())
	{
		ResultData->SetField(TEXT("return_value"), ReturnValue);
	}
	else
	{
		ResultData->SetField(TEXT("return_value"), MakeShared<FJsonValueNull>());
	}

	return CreateSuccessResponse(ResultData);
}

FString FExecPythonAction::EscapePythonString(const FString& Input)
{
	// Escape backslashes and triple-quotes for embedding in r"""..."""
	FString Result = Input;
	// Replace """ with \" \" \" to avoid breaking the raw string literal
	Result = Result.Replace(TEXT("\"\"\""), TEXT("\\\"\\\"\\\""));
	return Result;
}

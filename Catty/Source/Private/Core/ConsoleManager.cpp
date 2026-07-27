#include "Catty/Core/ConsoleManager.h"

#include "Catty/Core/ConfigFile.h"
#include "Catty/Core/Engine.h"
#include "Catty/Core/Log.h"

#include <cctype>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Catty
{

namespace
{

std::string ToLowerAscii(std::string Text)
{
	for (char& Ch : Text)
	{
		Ch = static_cast<char>(std::tolower(static_cast<unsigned char>(Ch)));
	}
	return Text;
}

bool ParseBoolToken(const std::string& Text, bool& OutValue)
{
	const std::string Lower = ToLowerAscii(Text);
	if (Lower == "1" || Lower == "true" || Lower == "yes" || Lower == "on")
	{
		OutValue = true;
		return true;
	}
	if (Lower == "0" || Lower == "false" || Lower == "no" || Lower == "off")
	{
		OutValue = false;
		return true;
	}
	return false;
}

class FConsoleVariableBool final : public IConsoleVariable
{
public:
	FConsoleVariableBool(std::string InName, bool InValue, std::string InHelp, EConsoleVariableFlags InFlags)
		: Name(std::move(InName))
		, Help(std::move(InHelp))
		, Flags(InFlags)
		, Value(InValue)
	{
	}

	[[nodiscard]] const std::string& GetName() const override { return Name; }
	[[nodiscard]] const std::string& GetHelp() const override { return Help; }
	[[nodiscard]] EConsoleVariableFlags GetFlags() const override { return Flags; }
	[[nodiscard]] EConsoleVariableType GetType() const override { return EConsoleVariableType::Bool; }

	[[nodiscard]] bool GetBool() const override { return Value; }
	[[nodiscard]] int GetInt() const override { return Value ? 1 : 0; }
	[[nodiscard]] float GetFloat() const override { return Value ? 1.0f : 0.0f; }
	[[nodiscard]] std::string GetString() const override { return Value ? "1" : "0"; }

	void Set(bool InValue) override { Value = InValue; }
	void Set(int InValue) override { Value = InValue != 0; }
	void Set(float InValue) override { Value = InValue != 0.0f; }
	void Set(const std::string& InValue) override { (void)SetFromString(InValue); }

	[[nodiscard]] bool SetFromString(const std::string& Text) override
	{
		bool Parsed = false;
		if (!ParseBoolToken(Text, Parsed))
		{
			return false;
		}
		Value = Parsed;
		return true;
	}

private:
	std::string Name;
	std::string Help;
	EConsoleVariableFlags Flags = EConsoleVariableFlags::Default;
	bool Value = false;
};

class FConsoleVariableInt final : public IConsoleVariable
{
public:
	FConsoleVariableInt(std::string InName, int InValue, std::string InHelp, EConsoleVariableFlags InFlags)
		: Name(std::move(InName))
		, Help(std::move(InHelp))
		, Flags(InFlags)
		, Value(InValue)
	{
	}

	[[nodiscard]] const std::string& GetName() const override { return Name; }
	[[nodiscard]] const std::string& GetHelp() const override { return Help; }
	[[nodiscard]] EConsoleVariableFlags GetFlags() const override { return Flags; }
	[[nodiscard]] EConsoleVariableType GetType() const override { return EConsoleVariableType::Int; }

	[[nodiscard]] bool GetBool() const override { return Value != 0; }
	[[nodiscard]] int GetInt() const override { return Value; }
	[[nodiscard]] float GetFloat() const override { return static_cast<float>(Value); }
	[[nodiscard]] std::string GetString() const override { return std::to_string(Value); }

	void Set(bool InValue) override { Value = InValue ? 1 : 0; }
	void Set(int InValue) override { Value = InValue; }
	void Set(float InValue) override { Value = static_cast<int>(InValue); }
	void Set(const std::string& InValue) override { (void)SetFromString(InValue); }

	[[nodiscard]] bool SetFromString(const std::string& Text) override
	{
		try
		{
			Value = std::stoi(Text);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

private:
	std::string Name;
	std::string Help;
	EConsoleVariableFlags Flags = EConsoleVariableFlags::Default;
	int Value = 0;
};

class FConsoleVariableFloat final : public IConsoleVariable
{
public:
	FConsoleVariableFloat(std::string InName, float InValue, std::string InHelp, EConsoleVariableFlags InFlags)
		: Name(std::move(InName))
		, Help(std::move(InHelp))
		, Flags(InFlags)
		, Value(InValue)
	{
	}

	[[nodiscard]] const std::string& GetName() const override { return Name; }
	[[nodiscard]] const std::string& GetHelp() const override { return Help; }
	[[nodiscard]] EConsoleVariableFlags GetFlags() const override { return Flags; }
	[[nodiscard]] EConsoleVariableType GetType() const override { return EConsoleVariableType::Float; }

	[[nodiscard]] bool GetBool() const override { return Value != 0.0f; }
	[[nodiscard]] int GetInt() const override { return static_cast<int>(Value); }
	[[nodiscard]] float GetFloat() const override { return Value; }
	[[nodiscard]] std::string GetString() const override { return std::to_string(Value); }

	void Set(bool InValue) override { Value = InValue ? 1.0f : 0.0f; }
	void Set(int InValue) override { Value = static_cast<float>(InValue); }
	void Set(float InValue) override { Value = InValue; }
	void Set(const std::string& InValue) override { (void)SetFromString(InValue); }

	[[nodiscard]] bool SetFromString(const std::string& Text) override
	{
		try
		{
			Value = std::stof(Text);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

private:
	std::string Name;
	std::string Help;
	EConsoleVariableFlags Flags = EConsoleVariableFlags::Default;
	float Value = 0.0f;
};

class FConsoleVariableString final : public IConsoleVariable
{
public:
	FConsoleVariableString(std::string InName, std::string InValue, std::string InHelp, EConsoleVariableFlags InFlags)
		: Name(std::move(InName))
		, Help(std::move(InHelp))
		, Flags(InFlags)
		, Value(std::move(InValue))
	{
	}

	[[nodiscard]] const std::string& GetName() const override { return Name; }
	[[nodiscard]] const std::string& GetHelp() const override { return Help; }
	[[nodiscard]] EConsoleVariableFlags GetFlags() const override { return Flags; }
	[[nodiscard]] EConsoleVariableType GetType() const override { return EConsoleVariableType::String; }

	[[nodiscard]] bool GetBool() const override
	{
		bool Parsed = false;
		return ParseBoolToken(Value, Parsed) && Parsed;
	}

	[[nodiscard]] int GetInt() const override
	{
		try
		{
			return std::stoi(Value);
		}
		catch (...)
		{
			return 0;
		}
	}

	[[nodiscard]] float GetFloat() const override
	{
		try
		{
			return std::stof(Value);
		}
		catch (...)
		{
			return 0.0f;
		}
	}

	[[nodiscard]] std::string GetString() const override { return Value; }

	void Set(bool InValue) override { Value = InValue ? "1" : "0"; }
	void Set(int InValue) override { Value = std::to_string(InValue); }
	void Set(float InValue) override { Value = std::to_string(InValue); }
	void Set(const std::string& InValue) override { Value = InValue; }

	[[nodiscard]] bool SetFromString(const std::string& Text) override
	{
		Value = Text;
		return true;
	}

private:
	std::string Name;
	std::string Help;
	EConsoleVariableFlags Flags = EConsoleVariableFlags::Default;
	std::string Value;
};

struct FConsoleManagerStorage
{
	mutable std::mutex Mutex;
	std::unordered_map<std::string, std::unique_ptr<IConsoleVariable>> Variables;
	std::vector<std::string> RegistrationOrder;
};

FConsoleManagerStorage& GetConsoleManagerStorage()
{
	static FConsoleManagerStorage Storage;
	return Storage;
}

template <typename TVariable, typename TValue>
IConsoleVariable* RegisterTyped(
	const char* Name,
	TValue DefaultValue,
	const char* Help,
	EConsoleVariableFlags Flags)
{
	if (!Name || Name[0] == '\0')
	{
		CATTY_CORE_ERROR("FConsoleManager::Register: empty name");
		return nullptr;
	}

	FConsoleManagerStorage& Storage = GetConsoleManagerStorage();
	const std::string Key = ToLowerAscii(Name);
	std::lock_guard<std::mutex> Lock(Storage.Mutex);

	const auto Existing = Storage.Variables.find(Key);
	if (Existing != Storage.Variables.end())
	{
		CATTY_CORE_WARN("FConsoleManager: CVar '{}' already registered — returning existing", Name);
		return Existing->second.get();
	}

	auto Variable = std::make_unique<TVariable>(
		Name,
		DefaultValue,
		Help ? Help : "",
		Flags);
	IConsoleVariable* Raw = Variable.get();
	Storage.Variables.emplace(Key, std::move(Variable));
	Storage.RegistrationOrder.push_back(Name);
	return Raw;
}

} // namespace

FConsoleManager& FConsoleManager::Get()
{
	static FConsoleManager Instance;
	return Instance;
}

IConsoleVariable* FConsoleManager::RegisterBool(
	const char* Name,
	bool DefaultValue,
	const char* Help,
	EConsoleVariableFlags Flags)
{
	return RegisterTyped<FConsoleVariableBool>(Name, DefaultValue, Help, Flags);
}

IConsoleVariable* FConsoleManager::RegisterInt(
	const char* Name,
	int DefaultValue,
	const char* Help,
	EConsoleVariableFlags Flags)
{
	return RegisterTyped<FConsoleVariableInt>(Name, DefaultValue, Help, Flags);
}

IConsoleVariable* FConsoleManager::RegisterFloat(
	const char* Name,
	float DefaultValue,
	const char* Help,
	EConsoleVariableFlags Flags)
{
	return RegisterTyped<FConsoleVariableFloat>(Name, DefaultValue, Help, Flags);
}

IConsoleVariable* FConsoleManager::RegisterString(
	const char* Name,
	const char* DefaultValue,
	const char* Help,
	EConsoleVariableFlags Flags)
{
	return RegisterTyped<FConsoleVariableString>(
		Name,
		std::string(DefaultValue ? DefaultValue : ""),
		Help,
		Flags);
}

IConsoleVariable* FConsoleManager::Find(const char* Name) const
{
	if (!Name)
	{
		return nullptr;
	}

	FConsoleManagerStorage& Storage = GetConsoleManagerStorage();
	const std::string Key = ToLowerAscii(Name);
	std::lock_guard<std::mutex> Lock(Storage.Mutex);
	const auto It = Storage.Variables.find(Key);
	return It != Storage.Variables.end() ? It->second.get() : nullptr;
}

int FConsoleManager::ApplyConsoleVariablesSection(const FConfigFile& Config, const char* SectionName)
{
	if (!SectionName)
	{
		SectionName = "ConsoleVariables";
	}

	int Applied = 0;
	for (const std::string& Key : Config.GetKeys(SectionName))
	{
		std::string Value;
		if (!Config.TryGetString(SectionName, Key, Value))
		{
			continue;
		}

		IConsoleVariable* Variable = Find(Key.c_str());
		if (!Variable)
		{
			CATTY_CORE_WARN("ConsoleVariables: unknown CVar '{}' (value='{}')", Key, Value);
			continue;
		}

		if ((Variable->GetFlags() & EConsoleVariableFlags::ReadOnly) != EConsoleVariableFlags::Default)
		{
			CATTY_CORE_WARN("ConsoleVariables: CVar '{}' is ReadOnly — ignored", Key);
			continue;
		}

		if (!Variable->SetFromString(Value))
		{
			CATTY_CORE_ERROR("ConsoleVariables: failed to parse '{}'='{}'", Key, Value);
			continue;
		}

		++Applied;
		CATTY_CORE_INFO("CVar {} = {}", Key, Variable->GetString());
	}

	return Applied;
}

int FConsoleManager::LoadConsoleVariablesFromIni(const std::string& IniFilePath)
{
	FConfigFile Config;
	if (!Config.Load(IniFilePath))
	{
		return -1;
	}

	if (!Config.HasSection("ConsoleVariables"))
	{
		return 0;
	}

	return ApplyConsoleVariablesSection(Config, "ConsoleVariables");
}

std::vector<std::string> FConsoleManager::GetNames() const
{
	FConsoleManagerStorage& Storage = GetConsoleManagerStorage();
	std::lock_guard<std::mutex> Lock(Storage.Mutex);
	return Storage.RegistrationOrder;
}

TAutoConsoleVariableBool::TAutoConsoleVariableBool(
	const char* Name,
	bool DefaultValue,
	const char* Help,
	EConsoleVariableFlags Flags)
	: Variable(FConsoleManager::Get().RegisterBool(Name, DefaultValue, Help, Flags))
{
}

bool TAutoConsoleVariableBool::GetValue() const
{
	return Variable ? Variable->GetBool() : false;
}

TAutoConsoleVariableInt::TAutoConsoleVariableInt(
	const char* Name,
	int DefaultValue,
	const char* Help,
	EConsoleVariableFlags Flags)
	: Variable(FConsoleManager::Get().RegisterInt(Name, DefaultValue, Help, Flags))
{
}

int TAutoConsoleVariableInt::GetValue() const
{
	return Variable ? Variable->GetInt() : 0;
}

TAutoConsoleVariableFloat::TAutoConsoleVariableFloat(
	const char* Name,
	float DefaultValue,
	const char* Help,
	EConsoleVariableFlags Flags)
	: Variable(FConsoleManager::Get().RegisterFloat(Name, DefaultValue, Help, Flags))
{
}

float TAutoConsoleVariableFloat::GetValue() const
{
	return Variable ? Variable->GetFloat() : 0.0f;
}

TAutoConsoleVariableString::TAutoConsoleVariableString(
	const char* Name,
	const char* DefaultValue,
	const char* Help,
	EConsoleVariableFlags Flags)
	: Variable(FConsoleManager::Get().RegisterString(Name, DefaultValue, Help, Flags))
{
}

std::string TAutoConsoleVariableString::GetValue() const
{
	return Variable ? Variable->GetString() : std::string{};
}

// Built-in engine CVars (defaults match FEngineConfig).
static TAutoConsoleVariableInt GCVarWindowWidth(
	"catty.Window.Width",
	1280,
	"Main window width in pixels");

static TAutoConsoleVariableInt GCVarWindowHeight(
	"catty.Window.Height",
	720,
	"Main window height in pixels");

static TAutoConsoleVariableBool GCVarWindowResizable(
	"catty.Window.Resizable",
	true,
	"Whether the main window is user-resizable");

static TAutoConsoleVariableBool GCVarCreateMainWindow(
	"catty.Window.Create",
	true,
	"Create an OS window (false = headless)");

static TAutoConsoleVariableFloat GCVarClearColorR(
	"catty.ClearColor.R",
	0.08f,
	"Default clear color R");

static TAutoConsoleVariableFloat GCVarClearColorG(
	"catty.ClearColor.G",
	0.10f,
	"Default clear color G");

static TAutoConsoleVariableFloat GCVarClearColorB(
	"catty.ClearColor.B",
	0.16f,
	"Default clear color B");

static TAutoConsoleVariableFloat GCVarClearColorA(
	"catty.ClearColor.A",
	1.0f,
	"Default clear color A");

void ApplyEngineCVarsToConfig(FEngineConfig& OutConfig)
{
	OutConfig.WindowWidth = GCVarWindowWidth.GetValue();
	OutConfig.WindowHeight = GCVarWindowHeight.GetValue();
	OutConfig.bResizableWindow = GCVarWindowResizable.GetValue();
	OutConfig.bCreateMainWindow = GCVarCreateMainWindow.GetValue();
	OutConfig.ClearColorR = GCVarClearColorR.GetValue();
	OutConfig.ClearColorG = GCVarClearColorG.GetValue();
	OutConfig.ClearColorB = GCVarClearColorB.GetValue();
	OutConfig.ClearColorA = GCVarClearColorA.GetValue();
}

} // namespace Catty

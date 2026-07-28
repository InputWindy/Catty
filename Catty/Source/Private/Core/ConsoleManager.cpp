#include "Catty/Core/ConsoleManager.h"

#include "Catty/Core/App.h"
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

class FConsoleVariableBase : public IConsoleVariable
{
public:
	FConsoleVariableBase(std::string InName, std::string InHelp, EConsoleVariableFlags InFlags)
		: Name(std::move(InName))
		, Help(std::move(InHelp))
		, Flags(InFlags)
	{
	}

	[[nodiscard]] const std::string& GetName() const override { return Name; }
	[[nodiscard]] const std::string& GetHelp() const override { return Help; }
	[[nodiscard]] EConsoleVariableFlags GetFlags() const override { return Flags; }
	[[nodiscard]] EConsoleVariableSetBy GetSetBy() const override { return SetBy; }

	[[nodiscard]] FDelegateHandle AddOnChangedCallback(FConsoleVariableChanged Callback) override
	{
		return OnChangedDelegate.AddLambda(std::move(Callback));
	}

	bool RemoveOnChangedCallback(FDelegateHandle Handle) override
	{
		return OnChangedDelegate.Remove(Handle);
	}

protected:
	[[nodiscard]] bool CanSet(EConsoleVariableSetBy InSetBy) const
	{
		if ((Flags & EConsoleVariableFlags::ReadOnly) != EConsoleVariableFlags::Default
			&& InSetBy == EConsoleVariableSetBy::Console)
		{
			return false;
		}
		return static_cast<std::uint32_t>(InSetBy) >= static_cast<std::uint32_t>(SetBy);
	}

	void CommitSetBy(EConsoleVariableSetBy InSetBy)
	{
		SetBy = InSetBy;
		OnChangedDelegate.Broadcast(*this);
	}

	std::string Name;
	std::string Help;
	EConsoleVariableFlags Flags = EConsoleVariableFlags::Default;
	EConsoleVariableSetBy SetBy = EConsoleVariableSetBy::Constructor;
	FOnConsoleVariableChanged OnChangedDelegate;
};
class FConsoleVariableBool final : public FConsoleVariableBase
{
public:
	FConsoleVariableBool(std::string InName, bool InValue, std::string InHelp, EConsoleVariableFlags InFlags)
		: FConsoleVariableBase(std::move(InName), std::move(InHelp), InFlags)
		, Value(InValue)
	{
	}

	[[nodiscard]] EConsoleVariableType GetType() const override { return EConsoleVariableType::Bool; }
	[[nodiscard]] bool GetBool() const override { return Value; }
	[[nodiscard]] int GetInt() const override { return Value ? 1 : 0; }
	[[nodiscard]] float GetFloat() const override { return Value ? 1.0f : 0.0f; }
	[[nodiscard]] std::string GetString() const override { return Value ? "1" : "0"; }

	bool Set(bool InValue, EConsoleVariableSetBy InSetBy) override
	{
		if (!CanSet(InSetBy))
		{
			return false;
		}
		Value = InValue;
		CommitSetBy(InSetBy);
		return true;
	}

	bool Set(int InValue, EConsoleVariableSetBy InSetBy) override { return Set(InValue != 0, InSetBy); }
	bool Set(float InValue, EConsoleVariableSetBy InSetBy) override { return Set(InValue != 0.0f, InSetBy); }
	bool Set(const std::string& InValue, EConsoleVariableSetBy InSetBy) override
	{
		return SetFromString(InValue, InSetBy);
	}

	[[nodiscard]] bool SetFromString(const std::string& Text, EConsoleVariableSetBy InSetBy) override
	{
		bool Parsed = false;
		if (!ParseBoolToken(Text, Parsed))
		{
			return false;
		}
		return Set(Parsed, InSetBy);
	}

private:
	bool Value = false;
};

class FConsoleVariableInt final : public FConsoleVariableBase
{
public:
	FConsoleVariableInt(std::string InName, int InValue, std::string InHelp, EConsoleVariableFlags InFlags)
		: FConsoleVariableBase(std::move(InName), std::move(InHelp), InFlags)
		, Value(InValue)
	{
	}

	[[nodiscard]] EConsoleVariableType GetType() const override { return EConsoleVariableType::Int; }
	[[nodiscard]] bool GetBool() const override { return Value != 0; }
	[[nodiscard]] int GetInt() const override { return Value; }
	[[nodiscard]] float GetFloat() const override { return static_cast<float>(Value); }
	[[nodiscard]] std::string GetString() const override { return std::to_string(Value); }

	bool Set(bool InValue, EConsoleVariableSetBy InSetBy) override { return Set(InValue ? 1 : 0, InSetBy); }

	bool Set(int InValue, EConsoleVariableSetBy InSetBy) override
	{
		if (!CanSet(InSetBy))
		{
			return false;
		}
		Value = InValue;
		CommitSetBy(InSetBy);
		return true;
	}

	bool Set(float InValue, EConsoleVariableSetBy InSetBy) override
	{
		return Set(static_cast<int>(InValue), InSetBy);
	}

	bool Set(const std::string& InValue, EConsoleVariableSetBy InSetBy) override
	{
		return SetFromString(InValue, InSetBy);
	}

	[[nodiscard]] bool SetFromString(const std::string& Text, EConsoleVariableSetBy InSetBy) override
	{
		try
		{
			return Set(std::stoi(Text), InSetBy);
		}
		catch (...)
		{
			return false;
		}
	}

private:
	int Value = 0;
};

class FConsoleVariableFloat final : public FConsoleVariableBase
{
public:
	FConsoleVariableFloat(std::string InName, float InValue, std::string InHelp, EConsoleVariableFlags InFlags)
		: FConsoleVariableBase(std::move(InName), std::move(InHelp), InFlags)
		, Value(InValue)
	{
	}

	[[nodiscard]] EConsoleVariableType GetType() const override { return EConsoleVariableType::Float; }
	[[nodiscard]] bool GetBool() const override { return Value != 0.0f; }
	[[nodiscard]] int GetInt() const override { return static_cast<int>(Value); }
	[[nodiscard]] float GetFloat() const override { return Value; }
	[[nodiscard]] std::string GetString() const override { return std::to_string(Value); }

	bool Set(bool InValue, EConsoleVariableSetBy InSetBy) override
	{
		return Set(InValue ? 1.0f : 0.0f, InSetBy);
	}

	bool Set(int InValue, EConsoleVariableSetBy InSetBy) override
	{
		return Set(static_cast<float>(InValue), InSetBy);
	}

	bool Set(float InValue, EConsoleVariableSetBy InSetBy) override
	{
		if (!CanSet(InSetBy))
		{
			return false;
		}
		Value = InValue;
		CommitSetBy(InSetBy);
		return true;
	}

	bool Set(const std::string& InValue, EConsoleVariableSetBy InSetBy) override
	{
		return SetFromString(InValue, InSetBy);
	}

	[[nodiscard]] bool SetFromString(const std::string& Text, EConsoleVariableSetBy InSetBy) override
	{
		try
		{
			return Set(std::stof(Text), InSetBy);
		}
		catch (...)
		{
			return false;
		}
	}

private:
	float Value = 0.0f;
};

class FConsoleVariableString final : public FConsoleVariableBase
{
public:
	FConsoleVariableString(std::string InName, std::string InValue, std::string InHelp, EConsoleVariableFlags InFlags)
		: FConsoleVariableBase(std::move(InName), std::move(InHelp), InFlags)
		, Value(std::move(InValue))
	{
	}

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

	bool Set(bool InValue, EConsoleVariableSetBy InSetBy) override
	{
		return Set(std::string(InValue ? "1" : "0"), InSetBy);
	}

	bool Set(int InValue, EConsoleVariableSetBy InSetBy) override
	{
		return Set(std::to_string(InValue), InSetBy);
	}

	bool Set(float InValue, EConsoleVariableSetBy InSetBy) override
	{
		return Set(std::to_string(InValue), InSetBy);
	}

	bool Set(const std::string& InValue, EConsoleVariableSetBy InSetBy) override
	{
		if (!CanSet(InSetBy))
		{
			return false;
		}
		Value = InValue;
		CommitSetBy(InSetBy);
		return true;
	}

	[[nodiscard]] bool SetFromString(const std::string& Text, EConsoleVariableSetBy InSetBy) override
	{
		return Set(Text, InSetBy);
	}

private:
	std::string Value;
};

struct FEarlyConsoleVariableSet
{
	std::string Value;
	EConsoleVariableSetBy SetBy = EConsoleVariableSetBy::ConsoleVariablesIni;
};

struct FConsoleManagerStorage
{
	mutable std::mutex Mutex;
	std::unordered_map<std::string, std::unique_ptr<IConsoleVariable>> Variables;
	std::unordered_map<std::string, FEarlyConsoleVariableSet> EarlySets;
	std::vector<std::string> RegistrationOrder;
};

FConsoleManagerStorage& GetConsoleManagerStorage()
{
	static FConsoleManagerStorage Storage;
	return Storage;
}

void QueueEarlySet_NoLock(
	FConsoleManagerStorage& Storage,
	const std::string& Key,
	const std::string& Value,
	EConsoleVariableSetBy SetBy)
{
	const auto Existing = Storage.EarlySets.find(Key);
	if (Existing != Storage.EarlySets.end()
		&& static_cast<std::uint32_t>(SetBy) < static_cast<std::uint32_t>(Existing->second.SetBy))
	{
		return;
	}
	Storage.EarlySets[Key] = FEarlyConsoleVariableSet{Value, SetBy};
}

void ApplyEarlySetIfAny(IConsoleVariable* Variable)
{
	if (!Variable)
	{
		return;
	}

	FConsoleManagerStorage& Storage = GetConsoleManagerStorage();
	const std::string Key = ToLowerAscii(Variable->GetName());

	FEarlyConsoleVariableSet Early;
	{
		std::lock_guard<std::mutex> Lock(Storage.Mutex);
		const auto It = Storage.EarlySets.find(Key);
		if (It == Storage.EarlySets.end())
		{
			return;
		}
		Early = It->second;
		Storage.EarlySets.erase(It);
	}

	if (Variable->SetFromString(Early.Value, Early.SetBy))
	{
		CATTY_CORE_INFO("CVar {} = {} (early set)", Variable->GetName(), Variable->GetString());
	}
	else
	{
		CATTY_CORE_WARN(
			"CVar early set failed: {}='{}' (SetBy={})",
			Variable->GetName(),
			Early.Value,
			static_cast<std::uint32_t>(Early.SetBy));
	}
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

	IConsoleVariable* Raw = nullptr;
	{
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
		Raw = Variable.get();
		Storage.Variables.emplace(Key, std::move(Variable));
		Storage.RegistrationOrder.push_back(Name);
	}

	ApplyEarlySetIfAny(Raw);
	return Raw;
}

bool SetByNameFromString(const char* Name, const char* Value, EConsoleVariableSetBy SetBy)
{
	if (!Name || !Value)
	{
		return false;
	}

	FConsoleManagerStorage& Storage = GetConsoleManagerStorage();
	const std::string Key = ToLowerAscii(Name);

	IConsoleVariable* Variable = nullptr;
	{
		std::lock_guard<std::mutex> Lock(Storage.Mutex);
		const auto It = Storage.Variables.find(Key);
		if (It != Storage.Variables.end())
		{
			Variable = It->second.get();
		}
		else
		{
			QueueEarlySet_NoLock(Storage, Key, Value, SetBy);
			CATTY_CORE_INFO("CVar early queue {} = {} (not registered yet)", Name, Value);
			return true;
		}
	}

	return Variable->SetFromString(Value, SetBy);
}

} // namespace

FConsoleManager& FConsoleManager::Get()
{
	if (GApp)
	{
		return GApp->GetConsoleManager();
	}
	static FConsoleManager PreAppFallback;
	return PreAppFallback;
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

bool FConsoleManager::TryGetBool(const char* Name, bool& OutValue) const
{
	if (const IConsoleVariable* Variable = Find(Name))
	{
		OutValue = Variable->GetBool();
		return true;
	}
	return false;
}

bool FConsoleManager::TryGetInt(const char* Name, int& OutValue) const
{
	if (const IConsoleVariable* Variable = Find(Name))
	{
		OutValue = Variable->GetInt();
		return true;
	}
	return false;
}

bool FConsoleManager::TryGetFloat(const char* Name, float& OutValue) const
{
	if (const IConsoleVariable* Variable = Find(Name))
	{
		OutValue = Variable->GetFloat();
		return true;
	}
	return false;
}

bool FConsoleManager::TryGetString(const char* Name, std::string& OutValue) const
{
	if (const IConsoleVariable* Variable = Find(Name))
	{
		OutValue = Variable->GetString();
		return true;
	}
	return false;
}

bool FConsoleManager::GetBool(const char* Name, bool DefaultValue) const
{
	bool Value = DefaultValue;
	(void)TryGetBool(Name, Value);
	return Value;
}

int FConsoleManager::GetInt(const char* Name, int DefaultValue) const
{
	int Value = DefaultValue;
	(void)TryGetInt(Name, Value);
	return Value;
}

float FConsoleManager::GetFloat(const char* Name, float DefaultValue) const
{
	float Value = DefaultValue;
	(void)TryGetFloat(Name, Value);
	return Value;
}

std::string FConsoleManager::GetString(const char* Name, const char* DefaultValue) const
{
	std::string Value;
	if (TryGetString(Name, Value))
	{
		return Value;
	}
	return DefaultValue ? DefaultValue : "";
}

bool FConsoleManager::SetBool(const char* Name, bool Value, EConsoleVariableSetBy SetBy)
{
	return SetFromString(Name, Value ? "1" : "0", SetBy);
}

bool FConsoleManager::SetInt(const char* Name, int Value, EConsoleVariableSetBy SetBy)
{
	return SetFromString(Name, std::to_string(Value).c_str(), SetBy);
}

bool FConsoleManager::SetFloat(const char* Name, float Value, EConsoleVariableSetBy SetBy)
{
	return SetFromString(Name, std::to_string(Value).c_str(), SetBy);
}

bool FConsoleManager::SetString(const char* Name, const char* Value, EConsoleVariableSetBy SetBy)
{
	return SetFromString(Name, Value ? Value : "", SetBy);
}

bool FConsoleManager::SetFromString(const char* Name, const char* Value, EConsoleVariableSetBy SetBy)
{
	return SetByNameFromString(Name, Value, SetBy);
}

int FConsoleManager::ApplyConsoleVariablesSection(
	const FConfigFile& Config,
	const char* SectionName,
	EConsoleVariableSetBy SetBy)
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

		if (SetFromString(Key.c_str(), Value.c_str(), SetBy))
		{
			++Applied;
			if (const IConsoleVariable* Variable = Find(Key.c_str()))
			{
				CATTY_CORE_INFO("CVar {} = {}", Key, Variable->GetString());
			}
		}
		else
		{
			CATTY_CORE_ERROR("ConsoleVariables: failed to apply '{}'='{}'", Key, Value);
		}
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

	return ApplyConsoleVariablesSection(Config, "ConsoleVariables", EConsoleVariableSetBy::ConsoleVariablesIni);
}

std::vector<std::string> FConsoleManager::GetNames() const
{
	FConsoleManagerStorage& Storage = GetConsoleManagerStorage();
	std::lock_guard<std::mutex> Lock(Storage.Mutex);
	return Storage.RegistrationOrder;
}

void FConsoleManager::Dump() const
{
	for (const std::string& Name : GetNames())
	{
		if (const IConsoleVariable* Variable = Find(Name.c_str()))
		{
			CATTY_CORE_INFO(
				"  {} = {} ({})",
				Variable->GetName(),
				Variable->GetString(),
				Variable->GetHelp());
		}
	}
}

static TAutoConsoleVariable GCVarAppName(
	"app.Name",
	"CattyApp",
	"Application display name (window title / client log name)");

static TAutoConsoleVariable GCVarWindowWidth(
	"catty.Window.Width",
	1280,
	"Main window width in pixels");

static TAutoConsoleVariable GCVarWindowHeight(
	"catty.Window.Height",
	720,
	"Main window height in pixels");

static TAutoConsoleVariable GCVarWindowResizable(
	"catty.Window.Resizable",
	true,
	"Whether the main window is user-resizable");

static TAutoConsoleVariable GCVarCreateMainWindow(
	"catty.Window.Create",
	true,
	"Create an OS window (false = headless)");

static TAutoConsoleVariable GCVarClearColorR(
	"catty.ClearColor.R",
	0.08f,
	"Default clear color R");

static TAutoConsoleVariable GCVarClearColorG(
	"catty.ClearColor.G",
	0.10f,
	"Default clear color G");

static TAutoConsoleVariable GCVarClearColorB(
	"catty.ClearColor.B",
	0.16f,
	"Default clear color B");

static TAutoConsoleVariable GCVarClearColorA(
	"catty.ClearColor.A",
	1.0f,
	"Default clear color A");

void ApplyEngineCVarsToConfig(FEngineConfig& OutConfig)
{
	const std::string AppName = FConsoleManager::Get().GetString("app.Name", OutConfig.ApplicationName.c_str());
	if (!AppName.empty())
	{
		OutConfig.ApplicationName = AppName;
	}

	OutConfig.WindowWidth = FConsoleManager::Get().GetInt("catty.Window.Width", OutConfig.WindowWidth);
	OutConfig.WindowHeight = FConsoleManager::Get().GetInt("catty.Window.Height", OutConfig.WindowHeight);
	OutConfig.bResizableWindow = FConsoleManager::Get().GetBool("catty.Window.Resizable", OutConfig.bResizableWindow);
	OutConfig.bCreateMainWindow = FConsoleManager::Get().GetBool("catty.Window.Create", OutConfig.bCreateMainWindow);
	OutConfig.ClearColorR = FConsoleManager::Get().GetFloat("catty.ClearColor.R", OutConfig.ClearColorR);
	OutConfig.ClearColorG = FConsoleManager::Get().GetFloat("catty.ClearColor.G", OutConfig.ClearColorG);
	OutConfig.ClearColorB = FConsoleManager::Get().GetFloat("catty.ClearColor.B", OutConfig.ClearColorB);
	OutConfig.ClearColorA = FConsoleManager::Get().GetFloat("catty.ClearColor.A", OutConfig.ClearColorA);
}

} // namespace Catty

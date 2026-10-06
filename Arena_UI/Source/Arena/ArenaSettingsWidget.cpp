#include "ArenaSettingsWidget.h"
#include "ArenaControls.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "ArenaGlassStyle.h"
#include "ArenaGlassButton.h"
#include "ArenaUISounds.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Engine.h"
#include "Engine/FontFace.h"
#include "Engine/GameViewportClient.h"
#include "Engine/UserInterfaceSettings.h"
#include "Fonts/CompositeFont.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/ConfigCacheIni.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "ArenaSettings"

namespace ArenaSettings
{
	enum EId
	{
		WindowMode, Resolution, VSync, FrameLimit, RenderMode,
		Brightness, UIScale, ColorBlind, ColorBlindStrength, MotionBlur,
		AutoAdjust, Preset, AntiAliasing, TemporalSR, NaniteGeometry, GlobalIllumination, Reflections,
		Shadows, ViewDistance, Textures, Effects, PostProcess,
		ShowFps,
		Count
	};

	struct FDef
	{
		FText Label;
		FText Description;
		TArray<FText> Options;
		int32 Default = 0;
		bool bButton = false;
	};

	const TCHAR* Section = TEXT("ArenaSettings");
	const TCHAR* EditGridColorKey = TEXT("EditGridColor");
	const TCHAR* BuildPreviewColorKey = TEXT("BuildPreviewColor");
	const TCHAR* BuildTextureIndexKey = TEXT("BuildTextureIndex");
	const FLinearColor DefaultEditGridColor(0.05f, 0.55f, 1.0f, 1.0f);
	const FLinearColor DefaultBuildPreviewColor(0.1f, 0.5f, 1.0f, 1.0f);
	const float FrameLimits[] = { 30.0f, 60.0f, 120.0f, 144.0f, 165.0f, 240.0f, 0.0f };
	const float TemporalScales[] = { 50.0f, 58.0f, 67.0f, 77.0f, 100.0f };
	const float UIScales[] = { 0.75f, 0.85f, 1.0f, 1.1f, 1.25f };

	TArray<FIntPoint> GetResolutions()
	{
		TArray<FIntPoint> Result;
		UKismetSystemLibrary::GetSupportedFullscreenResolutions(Result);
		TArray<FIntPoint> Unique;
		for (const FIntPoint& Point : Result)
		{
			Unique.AddUnique(Point);
		}
		if (Unique.IsEmpty())
		{
			Unique.Add(FIntPoint(1920, 1080));
		}
		return Unique;
	}

	FText ResolutionText(const FIntPoint& Point)
	{
		const int32 Divisor = FMath::GreatestCommonDivisor(Point.X, Point.Y);
		FIntPoint Ratio(Point.X / FMath::Max(Divisor, 1), Point.Y / FMath::Max(Divisor, 1));
		if (Ratio == FIntPoint(8, 5)) { Ratio = FIntPoint(16, 10); }
		return FText::FromString(FString::Printf(TEXT("%d:%d %d x %d"), Ratio.X, Ratio.Y, Point.X, Point.Y));
	}

	const FDef& Def(int32 Id)
	{
		static TArray<FDef> Defs;
		if (Defs.IsEmpty())
		{
			Defs.SetNum(Count);
			auto Set = [](FDef& D, const FText& Label, const FText& Desc, std::initializer_list<FText> Options, int32 Default)
			{
				D.Label = Label; D.Description = Desc; D.Options = Options; D.Default = Default;
			};
			const FText No = LOCTEXT("No", "No"), Yes = LOCTEXT("Yes", "Sí");
			const FText Low = LOCTEXT("Low", "Bajo"), Mid = LOCTEXT("Mid", "Medio"), High = LOCTEXT("High", "Alto"), Epic = LOCTEXT("Epic", "Épico");

			Set(Defs[WindowMode], LOCTEXT("WindowMode", "Modo ventana"),
				LOCTEXT("WindowModeDesc", "En el modo ventana puedes interactuar con otras ventanas con mayor facilidad y arrastrar los bordes de la ventana para ajustar el tamaño. En el modo ventana a pantalla completa puedes cambiar fácilmente de aplicación. En el modo pantalla completa no puedes interactuar con otras ventanas tan fácilmente, pero el juego funcionará algo más rápido."),
				{ LOCTEXT("Fullscreen", "Pantalla completa"), LOCTEXT("WindowedFullscreen", "Pantalla completa en ventana"), LOCTEXT("Windowed", "En ventana") }, 0);
			Set(Defs[Resolution], LOCTEXT("Resolution", "Resolución"),
				LOCTEXT("ResolutionDesc", "Resolución de la pantalla del juego. La resolución nativa de tu monitor da la imagen más nítida."), {}, 0);
			Set(Defs[VSync], LOCTEXT("VSync", "Sincronización vertical"),
				LOCTEXT("VSyncDesc", "Sincroniza la frecuencia de imágenes con la de la pantalla para evitar el efecto de imagen partida, a cambio de algo más de latencia."), { No, Yes }, 0);
			Set(Defs[FrameLimit], LOCTEXT("FrameLimit", "Límite de frecuencia de imágenes"),
				LOCTEXT("FrameLimitDesc", "Número máximo de imágenes por segundo que muestra el juego."),
				{ FText::FromString(TEXT("30 FPS")), FText::FromString(TEXT("60 FPS")), FText::FromString(TEXT("120 FPS")), FText::FromString(TEXT("144 FPS")), FText::FromString(TEXT("165 FPS")), FText::FromString(TEXT("240 FPS")), LOCTEXT("Unlimited", "Ilimitado") }, 6);
			Set(Defs[RenderMode], LOCTEXT("RenderMode", "Modo Renderizado"),
				LOCTEXT("RenderModeDesc", "Interfaz gráfica usada para renderizar el juego. Arena usa DirectX 12, necesario para Nanite, Lumen y las sombras virtuales."), { FText::FromString(TEXT("DIRECTX 12")) }, 0);

			TArray<FText> Percent;
			for (int32 Value = 50; Value <= 150; Value += 10)
			{
				Percent.Add(FText::FromString(FString::Printf(TEXT("%d %%"), Value)));
			}
			Defs[Brightness].Label = LOCTEXT("Brightness", "Brillo");
			Defs[Brightness].Description = LOCTEXT("BrightnessDesc", "Ajusta el brillo de la imagen del juego.");
			Defs[Brightness].Options = Percent;
			Defs[Brightness].Default = 5;
			Set(Defs[UIScale], LOCTEXT("UIScale", "Ajuste de la interfaz de usuario"),
				LOCTEXT("UIScaleDesc", "Cambia el tamaño de los menús y de la interfaz."),
				{ FText::FromString(TEXT("x0.75")), FText::FromString(TEXT("x0.85")), FText::FromString(TEXT("x1")), FText::FromString(TEXT("x1.1")), FText::FromString(TEXT("x1.25")) }, 2);
			Set(Defs[ColorBlind], LOCTEXT("ColorBlind", "Modo daltónico"),
				LOCTEXT("ColorBlindDesc", "Corrige los colores de la imagen para distintos tipos de daltonismo."),
				{ No, LOCTEXT("Deuteranope", "Deuteranopía"), LOCTEXT("Protanope", "Protanopía"), LOCTEXT("Tritanope", "Tritanopía") }, 0);
			TArray<FText> Strength;
			for (int32 Value = 0; Value <= 10; ++Value)
			{
				Strength.Add(FText::AsNumber(Value));
			}
			Defs[ColorBlindStrength].Label = LOCTEXT("ColorBlindStrength", "Intensidad de modo daltónico");
			Defs[ColorBlindStrength].Description = LOCTEXT("ColorBlindStrengthDesc", "Intensidad de la corrección del modo daltónico.");
			Defs[ColorBlindStrength].Options = Strength;
			Set(Defs[MotionBlur], LOCTEXT("MotionBlur", "Desenfoque de movimiento"),
				LOCTEXT("MotionBlurDesc", "Añade desenfoque a los objetos en movimiento y al girar la cámara."), { No, Yes }, 0);

			Defs[AutoAdjust].Label = LOCTEXT("AutoAdjust", "Ajustar calidad automáticamente");
			Defs[AutoAdjust].Description = LOCTEXT("AutoAdjustDesc", "Configura automáticamente las opciones de calidad gráfica en función del banco de pruebas del hardware.");
			Defs[AutoAdjust].Options = { LOCTEXT("AutoAdjustButton", "AJUSTAR AUTOMÁTICAMENTE") };
			Defs[AutoAdjust].bButton = true;
			Set(Defs[Preset], LOCTEXT("Preset", "Preestablecidos de calidad"),
				LOCTEXT("PresetDesc", "Aplica de una vez un nivel de calidad a sombras, distancia de visión, texturas, efectos y posprocesado."),
				{ Low, Mid, High, Epic, LOCTEXT("Custom", "Personalizado") }, 4);
			Set(Defs[AntiAliasing], LOCTEXT("AntiAliasing", "Suavizado de líneas (antialiasing)"),
				LOCTEXT("AntiAliasingDesc", "Suaviza los bordes dentados. TSR (superresolución temporal) da la imagen más nítida y estable."),
				{ No, FText::FromString(TEXT("FXAA")), FText::FromString(TEXT("TAA")), LOCTEXT("TSRLow", "TSR bajo"), LOCTEXT("TSRMid", "TSR medio"), LOCTEXT("TSRHigh", "TSR alto"), LOCTEXT("TSREpic", "TSR épico") }, 6);
			Set(Defs[TemporalSR], LOCTEXT("TemporalSR", "Superresolución temporal"),
				LOCTEXT("TemporalSRDesc", "La resolución 3D con la que se renderiza el juego antes de reconstruirla a la resolución de pantalla. Nativa es la más nítida; los otros modos ganan fotogramas."),
				{ LOCTEXT("TSRPerf", "Rendimiento"), LOCTEXT("TSRBalanced", "Equilibrado"), LOCTEXT("TSRQuality", "Calidad"), LOCTEXT("TSRRecommended", "Recomendado"), LOCTEXT("TSRNative", "Nativa") }, 4);
			Set(Defs[NaniteGeometry], LOCTEXT("Nanite", "Geometría virtualizada de Nanite"),
				LOCTEXT("NaniteDesc", "Renderiza los escenarios con geometría de muy alto detalle. Recomendado en tarjetas gráficas compatibles con DirectX 12."), { No, Yes }, 1);
			Set(Defs[GlobalIllumination], LOCTEXT("GI", "Iluminación global"),
				LOCTEXT("GIDesc", "Cómo rebota la luz en el escenario. La oclusión ambiental es rápida; Lumen calcula la luz rebotada en tiempo real."),
				{ No, LOCTEXT("AO", "Oclusión ambiental"), LOCTEXT("LumenHigh", "Lumen alto"), LOCTEXT("LumenEpic", "Lumen épico") }, 1);
			Set(Defs[Reflections], LOCTEXT("Reflections", "Reflejos"),
				LOCTEXT("ReflectionsDesc", "Calidad de los reflejos en superficies brillantes, agua y cristales."),
				{ No, LOCTEXT("SSR", "Espacio de pantalla"), LOCTEXT("LumenHigh2", "Lumen alto"), LOCTEXT("LumenEpic2", "Lumen épico") }, 1);
			Set(Defs[Shadows], LOCTEXT("Shadows", "Sombras"), LOCTEXT("ShadowsDesc", "Calidad y distancia de las sombras."), { No, Mid, High, Epic }, 3);
			Set(Defs[ViewDistance], LOCTEXT("ViewDistance", "Distancia de visión"), LOCTEXT("ViewDistanceDesc", "Distancia a la que se dibujan los objetos y su nivel de detalle."),
				{ LOCTEXT("Near", "Cercana"), Mid, LOCTEXT("Far", "Lejana"), Epic }, 3);
			Set(Defs[Textures], LOCTEXT("Textures", "Texturas"), LOCTEXT("TexturesDesc", "Resolución de las texturas. Épico usa más memoria de vídeo."), { Low, Mid, High, Epic }, 3);
			Set(Defs[Effects], LOCTEXT("Effects", "Efectos"), LOCTEXT("EffectsDesc", "Calidad de las partículas y efectos visuales."), { Low, Mid, High, Epic }, 3);
			Set(Defs[PostProcess], LOCTEXT("PostProcess", "Posprocesado"), LOCTEXT("PostProcessDesc", "Calidad del bloom, la profundidad de campo y otros efectos de imagen."), { Low, Mid, High, Epic }, 3);
			Set(Defs[ShowFps], LOCTEXT("ShowFps", "Mostrar FPS"), LOCTEXT("ShowFpsDesc", "Muestra las imágenes por segundo en pantalla."), { No, Yes }, 1);
		}
		return Defs[Id];
	}

	int32 NumOptions(int32 Id)
	{
		return Id == Resolution ? GetResolutions().Num() : Def(Id).Options.Num();
	}

	int32 DefaultResolutionIndex()
	{
		const TArray<FIntPoint> List = GetResolutions();
		const FIntPoint Desktop = UGameUserSettings::GetGameUserSettings()->GetDesktopResolution();
		const int32 Found = List.IndexOfByKey(Desktop);
		return Found != INDEX_NONE ? Found : List.Num() - 1;
	}

	int32 Get(int32 Id)
	{
		if (Id == Resolution)
		{
			FString Saved;
			if (GConfig->GetString(Section, TEXT("Resolution"), Saved, GGameUserSettingsIni))
			{
				FString X, Y;
				if (Saved.Split(TEXT("x"), &X, &Y))
				{
					const int32 Found = GetResolutions().IndexOfByKey(FIntPoint(FCString::Atoi(*X), FCString::Atoi(*Y)));
					if (Found != INDEX_NONE)
					{
						return Found;
					}
				}
			}
			return DefaultResolutionIndex();
		}
		int32 Value = Def(Id).Default;
		GConfig->GetInt(Section, *FString::Printf(TEXT("Setting%d"), Id), Value, GGameUserSettingsIni);
		return FMath::Clamp(Value, 0, FMath::Max(NumOptions(Id) - 1, 0));
	}

	void Store(int32 Id, int32 Value)
	{
		if (Id == Resolution)
		{
			const TArray<FIntPoint> List = GetResolutions();
			const FIntPoint Point = List.IsValidIndex(Value) ? List[Value] : List.Last();
			GConfig->SetString(Section, TEXT("Resolution"), *FString::Printf(TEXT("%dx%d"), Point.X, Point.Y), GGameUserSettingsIni);
			return;
		}
		GConfig->SetInt(Section, *FString::Printf(TEXT("Setting%d"), Id), Value, GGameUserSettingsIni);
	}

	FLinearColor GetEditGridColor()
	{
		FColor SavedColor = DefaultEditGridColor.ToFColor(true);
		GConfig->GetColor(Section, EditGridColorKey, SavedColor, GGameUserSettingsIni);
		return FLinearColor::FromSRGBColor(SavedColor);
	}

	FLinearColor GetBuildPreviewColor()
	{
		FColor SavedColor = DefaultBuildPreviewColor.ToFColor(true);
		GConfig->GetColor(Section, BuildPreviewColorKey, SavedColor, GGameUserSettingsIni);
		return FLinearColor::FromSRGBColor(SavedColor);
	}

	int32 GetBuildTextureIndex()
	{
		int32 Index = 1;
		GConfig->GetInt(Section, BuildTextureIndexKey, Index, GGameUserSettingsIni);
		return FMath::Clamp(Index, 1, 4);
	}

	void StoreEditGridColor(const FLinearColor& Color)
	{
		GConfig->SetColor(Section, EditGridColorKey, Color.ToFColor(true), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}

	void StoreBuildPreviewColor(const FLinearColor& Color)
	{
		GConfig->SetColor(Section, BuildPreviewColorKey, Color.ToFColor(true), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}

	void StoreBuildTextureIndex(int32 Index)
	{
		GConfig->SetInt(Section, BuildTextureIndexKey, FMath::Clamp(Index, 1, 4), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}

	void SetCVar(const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Var->Set(Value, ECVF_SetByCode);
		}
	}

	void ApplyAll()
	{
		UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings();
		if (Settings == nullptr)
		{
			return;
		}

		// Window and resolution only in a packaged/standalone game: in the editor they would resize the editor
		if (!GIsEditor)
		{
			const TArray<FIntPoint> List = GetResolutions();
			const int32 Mode = Get(WindowMode);
			Settings->SetFullscreenMode(Mode == 0 ? EWindowMode::Fullscreen : (Mode == 1 ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed));
			Settings->SetScreenResolution(List.IsValidIndex(Get(Resolution)) ? List[Get(Resolution)] : List.Last());
		}
		Settings->SetVSyncEnabled(Get(VSync) == 1);
		Settings->SetFrameRateLimit(FrameLimits[Get(FrameLimit)]);

		const int32 AA = Get(AntiAliasing);
		Settings->SetAntiAliasingQuality(AA >= 3 ? AA - 3 : 2);
		Settings->SetShadowQuality(Get(Shadows));
		Settings->SetViewDistanceQuality(Get(ViewDistance));
		Settings->SetTextureQuality(Get(Textures));
		Settings->SetVisualEffectQuality(Get(Effects));
		Settings->SetPostProcessingQuality(Get(PostProcess));
		const int32 GI = Get(GlobalIllumination);
		Settings->SetGlobalIlluminationQuality(GI >= 2 ? GI : 1);
		const int32 Refl = Get(Reflections);
		Settings->SetReflectionQuality(Refl >= 2 ? Refl : 1);
		Settings->ApplySettings(false);
		Settings->SaveSettings();

		// Renderer switches (project settings level cvars need code priority)
		SetCVar(TEXT("r.AntiAliasingMethod"), AA == 0 ? 0.0f : (AA == 1 ? 1.0f : (AA == 2 ? 2.0f : 4.0f)));
		SetCVar(TEXT("r.ScreenPercentage"), TemporalScales[Get(TemporalSR)]);
		SetCVar(TEXT("r.Nanite"), Get(NaniteGeometry) == 1 ? 1.0f : 0.0f);
		SetCVar(TEXT("r.DynamicGlobalIlluminationMethod"), GI >= 2 ? 1.0f : 0.0f);
		SetCVar(TEXT("r.AmbientOcclusionLevels"), GI == 0 ? 0.0f : -1.0f);
		SetCVar(TEXT("r.ReflectionMethod"), Refl == 0 ? 0.0f : (Refl == 1 ? 2.0f : 1.0f));
		SetCVar(TEXT("r.SSR.Quality"), Refl == 0 ? 0.0f : 3.0f);
		SetCVar(TEXT("r.MotionBlurQuality"), Get(MotionBlur) == 1 ? 4.0f : 0.0f);

		if (GEngine)
		{
			GEngine->DisplayGamma = 2.2f * (0.5f + 0.1f * Get(Brightness));
		}
		GetMutableDefault<UUserInterfaceSettings>()->ApplicationScale = UIScales[Get(UIScale)];
		UWidgetBlueprintLibrary::SetColorVisionDeficiencyType(static_cast<EColorVisionDeficiency>(Get(ColorBlind)), float(Get(ColorBlindStrength)), true, false);

		if (GEngine && GEngine->GameViewport && GEngine->GameViewport->GetWorld())
		{
			const bool bShow = Get(ShowFps) == 1;
			if (GEngine->GameViewport->IsStatEnabled(TEXT("FPS")) != bShow)
			{
				GEngine->Exec(GEngine->GameViewport->GetWorld(), TEXT("stat fps"));
			}
		}
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

// ---------------------------------------------------------------------------------------------------------------

void UArenaSettingRowHandler::HandleNext() { if (Owner.IsValid()) { Owner->HoverRow(Row); Owner->StepRow(Row, 1); } }
void UArenaSettingRowHandler::HandlePrev() { if (Owner.IsValid()) { Owner->HoverRow(Row); Owner->StepRow(Row, -1); } }
void UArenaSettingRowHandler::HandleHovered() { if (Owner.IsValid()) { Owner->HoverRow(Row); } }
void UArenaSettingRowHandler::HandleTab() { if (Owner.IsValid()) { Owner->ShowTab(Row); } }
void UArenaSettingRowHandler::HandleKeyRow() { if (Owner.IsValid()) { Owner->BeginCapture(Row); } }

UArenaSettingsWidget::UArenaSettingsWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
	static ConstructorHelpers::FObjectFinder<UFontFace> HeadingFinder(TEXT("/Game/UI/Fortnite/Fonts/FF_BurbankBigCondensed_Black.FF_BurbankBigCondensed_Black"));
	static ConstructorHelpers::FObjectFinder<UFontFace> BodyFinder(TEXT("/Game/UI/Fortnite/Fonts/FF_BurbankSmall_Black.FF_BurbankSmall_Black"));
	HeadingFontFace = HeadingFinder.Object;
	BodyFontFace = BodyFinder.Object;
}

UArenaSettingsWidget* UArenaSettingsWidget::Open(APlayerController* PC, UUserWidget* InFocusOnClose)
{
	if (PC == nullptr)
	{
		return nullptr;
	}
	UArenaSettingsWidget* Widget = CreateWidget<UArenaSettingsWidget>(PC, UArenaSettingsWidget::StaticClass());
	if (Widget == nullptr)
	{
		return nullptr;
	}
	Widget->FocusOnClose = InFocusOnClose;
	Widget->AddToViewport(100);
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(Widget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(InputMode);
	PC->SetShowMouseCursor(true);
	return Widget;
}

void UArenaSettingsWidget::ApplySavedSettings()
{
	ArenaSettings::ApplyAll();
}

FLinearColor UArenaSettingsWidget::GetEditGridColor()
{
	return ArenaSettings::GetEditGridColor();
}

void UArenaSettingsWidget::SetEditGridColor(FLinearColor Color)
{
	ArenaSettings::StoreEditGridColor(Color);
}

FLinearColor UArenaSettingsWidget::GetBuildPreviewColor()
{
	return ArenaSettings::GetBuildPreviewColor();
}

void UArenaSettingsWidget::SetBuildPreviewColor(FLinearColor Color)
{
	ArenaSettings::StoreBuildPreviewColor(Color);
}

int32 UArenaSettingsWidget::GetBuildTextureIndex()
{
	return ArenaSettings::GetBuildTextureIndex();
}

void UArenaSettingsWidget::SetBuildTextureIndex(int32 Index)
{
	ArenaSettings::StoreBuildTextureIndex(Index);
}

void UArenaSettingsWidget::Close()
{
	APlayerController* PC = GetOwningPlayer();
	RemoveFromParent();
	if (PC == nullptr)
	{
		return;
	}
	if (UUserWidget* Previous = FocusOnClose.Get())
	{
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(Previous->TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(InputMode);
		PC->SetShowMouseCursor(true);
	}
	else
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);
		PC->SetIgnoreMoveInput(false);
		PC->SetIgnoreLookInput(false);
	}
}

void UArenaSettingsWidget::ResetToDefaults()
{
	using namespace ArenaSettings;
	if (ActiveTab == 3)
	{
		ArenaControls::ResetAll();
		CaptureAction = INDEX_NONE;
		RefreshKeys();
		return;
	}
	for (int32 Id = 0; Id < Count; ++Id)
	{
		Store(Id, Id == Resolution ? DefaultResolutionIndex() : Def(Id).Default);
	}
	SetEditGridColor(ArenaSettings::DefaultEditGridColor);
	SetBuildPreviewColor(ArenaSettings::DefaultBuildPreviewColor);
	SetBuildTextureIndex(1);
	ApplyAll();
	for (int32 Row = 0; Row < RowSettings.Num(); ++Row)
	{
		RefreshRow(Row);
	}
	RefreshEditGridColorSwatch();
	RefreshBuildPreviewColorSwatch();
	RefreshBuildTextureValue();
	RefreshDescription();
}

void UArenaSettingsWidget::StepRow(int32 Row, int32 Direction)
{
	using namespace ArenaSettings;
	if (!RowSettings.IsValidIndex(Row))
	{
		return;
	}
	const int32 Id = RowSettings[Row];
	if (Id == AutoAdjust)
	{
		if (UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings())
		{
			Settings->RunHardwareBenchmark();
			Settings->ApplyHardwareBenchmarkResults();
			Store(Shadows, Settings->GetShadowQuality());
			Store(ViewDistance, Settings->GetViewDistanceQuality());
			Store(Textures, Settings->GetTextureQuality());
			Store(Effects, Settings->GetVisualEffectQuality());
			Store(PostProcess, Settings->GetPostProcessingQuality());
			Store(AntiAliasing, 3 + FMath::Clamp(Settings->GetAntiAliasingQuality(), 0, 3));
			Store(Preset, FMath::Clamp(Settings->GetOverallScalabilityLevel(), 0, 3));
		}
	}
	else
	{
		const int32 Num = NumOptions(Id);
		if (Num <= 1)
		{
			return;
		}
		const int32 Value = (Get(Id) + Direction + Num) % Num;
		Store(Id, Value);
		if (Id == Preset && Value < 4)
		{
			const int32 Qualities[] = { Shadows, ViewDistance, Textures, Effects, PostProcess };
			for (const int32 Quality : Qualities)
			{
				Store(Quality, Value);
			}
			Store(AntiAliasing, 3 + Value);
		}
		else if (Id == Shadows || Id == ViewDistance || Id == Textures || Id == Effects || Id == PostProcess || Id == AntiAliasing)
		{
			Store(Preset, 4);
		}
	}
	ApplyAll();
	for (int32 Other = 0; Other < RowSettings.Num(); ++Other)
	{
		RefreshRow(Other);
	}
	RefreshDescription();
}

void UArenaSettingsWidget::HoverRow(int32 Row)
{
	if (!RowSettings.IsValidIndex(Row) || Row == HoveredRow)
	{
		return;
	}
	const int32 Previous = HoveredRow;
	HoveredRow = Row;
	RefreshRow(Previous);
	RefreshRow(Row);
	RefreshDescription();
}

// ---------------------------------------------------------------------------------------------------------------

FSlateFontInfo UArenaSettingsWidget::MakeFont(UFontFace* Face, int32 Size) const
{
	if (Face == nullptr)
	{
		return FCoreStyle::GetDefaultFontStyle("Bold", Size);
	}
	static TMap<TWeakObjectPtr<UFontFace>, TSharedPtr<FCompositeFont>> Cache;
	TSharedPtr<FCompositeFont>& Font = Cache.FindOrAdd(Face);
	if (!Font.IsValid())
	{
		Font = MakeShared<FStandaloneCompositeFont>();
		FTypefaceEntry Entry(TEXT("Regular"));
		Entry.Font = FFontData(Face);
		Font->DefaultTypeface.Fonts.Add(Entry);
	}
	return FSlateFontInfo(Font, Size);
}

UTextBlock* UArenaSettingsWidget::MakeText(const FText& Text, UFontFace* Face, int32 Size, const FLinearColor& Color)
{
	UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>();
	Block->SetText(Text);
	Block->SetFont(Face ? MakeFont(Face, Size) : FCoreStyle::GetDefaultFontStyle("Bold", Size));
	Block->SetColorAndOpacity(FSlateColor(Color));
	Block->SetVisibility(ESlateVisibility::HitTestInvisible);
	return Block;
}

UButton* UArenaSettingsWidget::MakeButton(const FLinearColor& Normal, const FLinearColor& Hovered, float Radius)
{
	UButton* Button = WidgetTree->ConstructWidget<UArenaGlassButton>();
	FButtonStyle Style;
	Style.SetNormal(FSlateRoundedBoxBrush(Normal, Radius));
	Style.SetHovered(FSlateRoundedBoxBrush(Hovered, Radius));
	Style.SetPressed(FSlateRoundedBoxBrush(Hovered, Radius));
	Style.SetNormalPadding(FMargin(0.0f));
	Style.SetPressedPadding(FMargin(0.0f));
	Button->SetStyle(Style);
	Button->OnHovered.AddDynamic(this, &UArenaSettingsWidget::HandleButtonHovered);
	return Button;
}

void UArenaSettingsWidget::HandleButtonHovered()
{
	ArenaUISounds::PlayHover(this);
}

TSharedRef<SWidget> UArenaSettingsWidget::RebuildWidget()
{
	if (WidgetTree && WidgetTree->RootWidget == nullptr)
	{
		BuildLayout();
	}
	return Super::RebuildWidget();
}

void UArenaSettingsWidget::AddSection(UVerticalBox* List, const FText& Title)
{
	using namespace ArenaGlass;
	UTextBlock* Text = MakeText(FText::FromString(Title.ToString().ToUpper()), BodyFontFace, 12, Dim);
	List->AddChildToVerticalBox(Text)->SetPadding(FMargin(8.0f, 18.0f, 0.0f, 6.0f));
}

void UArenaSettingsWidget::AddRow(UVerticalBox* List, int32 Setting)
{
	using namespace ArenaSettings;
	const int32 Row = RowSettings.Add(Setting);
	FRowWidgets& Widgets = RowWidgets.AddDefaulted_GetRef();

	UArenaSettingRowHandler* Handler = NewObject<UArenaSettingRowHandler>(this);
	Handler->Row = Row;
	Handler->Owner = this;
	Handlers.Add(Handler);

	// Hover frame: a pane of glass lights up under the row
	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>();
	Frame->SetPadding(FMargin(14.0f, 5.0f, 10.0f, 5.0f));
	Widgets.Frame = Frame;

	UButton* RowButton = MakeButton(FLinearColor(0, 0, 0, 0), FLinearColor(1, 1, 1, 0.03f), 12.0f);
	RowButton->OnHovered.AddDynamic(Handler, &UArenaSettingRowHandler::HandleHovered);
	RowButton->OnClicked.AddDynamic(Handler, &UArenaSettingRowHandler::HandleNext);
	Frame->SetContent(RowButton);

	UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (UButtonSlot* RowSlot = Cast<UButtonSlot>(RowButton->SetContent(Line)))
	{
		RowSlot->SetHorizontalAlignment(HAlign_Fill);
		RowSlot->SetVerticalAlignment(VAlign_Center);
	}

	USizeBox* LabelBox = WidgetTree->ConstructWidget<USizeBox>();
	UTextBlock* Label = MakeText(Def(Setting).Label, BodyFontFace, 15, ArenaGlass::Ink);
	Label->SetClipping(EWidgetClipping::ClipToBounds);
	LabelBox->SetContent(Label);
	UHorizontalBoxSlot* LabelSlot = Line->AddChildToHorizontalBox(LabelBox);
	LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	LabelSlot->SetVerticalAlignment(VAlign_Center);

	if (Def(Setting).bButton)
	{
		UButton* Action = MakeButton(FLinearColor(0, 0, 0, 0), FLinearColor(0, 0, 0, 0), 14.0f);
		ArenaGlass::Style(Action, ArenaGlass::ButtonStyle(0.18f, 14.0f, 0.5f, ArenaGlass::Mint));
		Action->OnClicked.AddDynamic(Handler, &UArenaSettingRowHandler::HandleNext);
		Action->OnHovered.AddDynamic(Handler, &UArenaSettingRowHandler::HandleHovered);
		USizeBox* ActionBox = WidgetTree->ConstructWidget<USizeBox>();
		ActionBox->SetWidthOverride(200.0f);
		ActionBox->SetHeightOverride(26.0f);
		UTextBlock* ActionText = MakeText(Def(Setting).Options[0], BodyFontFace, 12, ArenaGlass::Ink);
		if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(Action->SetContent(ActionText)))
		{
			ButtonSlot->SetHorizontalAlignment(HAlign_Center);
			ButtonSlot->SetVerticalAlignment(VAlign_Center);
		}
		ActionBox->SetContent(Action);
		Line->AddChildToHorizontalBox(ActionBox)->SetVerticalAlignment(VAlign_Center);
		List->AddChildToVerticalBox(Frame);
		return;
	}

	auto MakeArrow = [this, Handler](bool bNext)
	{
		UButton* Arrow = MakeButton(FLinearColor(0, 0, 0, 0), FLinearColor(0, 0, 0, 0), 10.0f);
		ArenaGlass::Style(Arrow, ArenaGlass::ButtonStyle(0.08f, 11.0f, 0.26f));
		if (bNext)
		{
			Arrow->OnClicked.AddDynamic(Handler, &UArenaSettingRowHandler::HandleNext);
		}
		else
		{
			Arrow->OnClicked.AddDynamic(Handler, &UArenaSettingRowHandler::HandlePrev);
		}
		Arrow->OnHovered.AddDynamic(Handler, &UArenaSettingRowHandler::HandleHovered);
		UTextBlock* Glyph = MakeText(FText::FromString(bNext ? TEXT(">") : TEXT("<")), BodyFontFace, 12, ArenaGlass::Ink);
		if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(Arrow->SetContent(Glyph)))
		{
			ButtonSlot->SetPadding(FMargin(8.0f, 2.0f));
			ButtonSlot->SetHorizontalAlignment(HAlign_Center);
			ButtonSlot->SetVerticalAlignment(VAlign_Center);
		}
		return Arrow;
	};

	Line->AddChildToHorizontalBox(MakeArrow(false))->SetVerticalAlignment(VAlign_Center);

	USizeBox* ValueBox = WidgetTree->ConstructWidget<USizeBox>();
	ValueBox->SetWidthOverride(220.0f);
	UVerticalBox* ValueColumn = WidgetTree->ConstructWidget<UVerticalBox>();
	ValueBox->SetContent(ValueColumn);
	Widgets.Value = MakeText(FText::GetEmpty(), BodyFontFace, 15, ArenaGlass::Ink);
	ValueColumn->AddChildToVerticalBox(Widgets.Value)->SetHorizontalAlignment(HAlign_Center);

	UHorizontalBox* Bars = WidgetTree->ConstructWidget<UHorizontalBox>();
	const int32 Num = FMath::Min(NumOptions(Setting), 16);
	for (int32 Index = 0; Index < Num; ++Index)
	{
		UBorder* Segment = WidgetTree->ConstructWidget<UBorder>();
		Segment->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White, 2.0f));
		USizeBox* SegmentBox = WidgetTree->ConstructWidget<USizeBox>();
		SegmentBox->SetHeightOverride(3.0f);
		Segment->SetContent(SegmentBox);
		UHorizontalBoxSlot* SegmentSlot = Bars->AddChildToHorizontalBox(Segment);
		SegmentSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		SegmentSlot->SetPadding(FMargin(Num > 8 ? 2.0f : 5.0f, 0.0f));
		Widgets.Segments.Add(Segment);
	}
	ValueColumn->AddChildToVerticalBox(Bars)->SetPadding(FMargin(0.0f, 5.0f, 0.0f, 0.0f));
	Line->AddChildToHorizontalBox(ValueBox)->SetVerticalAlignment(VAlign_Center);
	Line->AddChildToHorizontalBox(MakeArrow(true))->SetVerticalAlignment(VAlign_Center);

	List->AddChildToVerticalBox(Frame);
}

void UArenaSettingsWidget::AddEditGridColorRow(UVerticalBox* List)
{
	using namespace ArenaGlass;
	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>();
	Frame->SetPadding(FMargin(14.0f, 5.0f, 10.0f, 5.0f));
	Frame->SetBrush(Surface(0.0f, 14.0f, 0.0f));

	UButton* Button = MakeButton(FLinearColor(0, 0, 0, 0), FLinearColor(1, 1, 1, 0.03f), 12.0f);
	Button->OnClicked.AddDynamic(this, &UArenaSettingsWidget::HandleEditGridColorClicked);
	Frame->SetContent(Button);

	UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(Button->SetContent(Line)))
	{
		ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
		ButtonSlot->SetVerticalAlignment(VAlign_Center);
	}

	UTextBlock* Label = MakeText(LOCTEXT("EditGridColor", "Color de la rejilla al editar"), BodyFontFace, 15, Ink);
	Line->AddChildToHorizontalBox(Label)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	USizeBox* SwatchBox = WidgetTree->ConstructWidget<USizeBox>();
	SwatchBox->SetWidthOverride(82.0f);
	SwatchBox->SetHeightOverride(28.0f);
	EditGridColorSwatch = WidgetTree->ConstructWidget<UBorder>();
	EditGridColorSwatch->SetPadding(FMargin(2.0f));
	SwatchBox->SetContent(EditGridColorSwatch);
	Line->AddChildToHorizontalBox(SwatchBox)->SetVerticalAlignment(VAlign_Center);

	List->AddChildToVerticalBox(Frame);
	RefreshEditGridColorSwatch();
}

void UArenaSettingsWidget::RefreshEditGridColorSwatch()
{
	if (EditGridColorSwatch)
	{
		EditGridColorSwatch->SetBrush(FSlateRoundedBoxBrush(GetEditGridColor(), 6.0f));
	}
}

void UArenaSettingsWidget::HandleEditGridColorClicked()
{
	OpenGridColorPicker(false);
}

void UArenaSettingsWidget::AddBuildPreviewColorRow(UVerticalBox* List)
{
	using namespace ArenaGlass;
	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>();
	Frame->SetPadding(FMargin(14.0f, 5.0f, 10.0f, 5.0f));
	Frame->SetBrush(Surface(0.0f, 14.0f, 0.0f));

	UButton* Button = MakeButton(FLinearColor(0, 0, 0, 0), FLinearColor(1, 1, 1, 0.03f), 12.0f);
	Button->OnClicked.AddDynamic(this, &UArenaSettingsWidget::HandleBuildPreviewColorClicked);
	Frame->SetContent(Button);

	UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(Button->SetContent(Line)))
	{
		ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
		ButtonSlot->SetVerticalAlignment(VAlign_Center);
	}

	UTextBlock* Label = MakeText(LOCTEXT("BuildPreviewColor", "Color de la previsualización al construir"), BodyFontFace, 15, Ink);
	Line->AddChildToHorizontalBox(Label)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	USizeBox* SwatchBox = WidgetTree->ConstructWidget<USizeBox>();
	SwatchBox->SetWidthOverride(82.0f);
	SwatchBox->SetHeightOverride(28.0f);
	BuildPreviewColorSwatch = WidgetTree->ConstructWidget<UBorder>();
	BuildPreviewColorSwatch->SetPadding(FMargin(2.0f));
	SwatchBox->SetContent(BuildPreviewColorSwatch);
	Line->AddChildToHorizontalBox(SwatchBox)->SetVerticalAlignment(VAlign_Center);

	List->AddChildToVerticalBox(Frame);
	RefreshBuildPreviewColorSwatch();
}

void UArenaSettingsWidget::AddBuildTextureRow(UVerticalBox* List)
{
	using namespace ArenaGlass;
	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>();
	Frame->SetPadding(FMargin(14.0f, 5.0f, 10.0f, 5.0f));
	Frame->SetBrush(Surface(0.0f, 14.0f, 0.0f));

	UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>();
	Frame->SetContent(Line);
	UTextBlock* Label = MakeText(LOCTEXT("BuildTexture", "Textura de construcción"), BodyFontFace, 15, Ink);
	Line->AddChildToHorizontalBox(Label)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	auto MakeTextureArrow = [this, Line](bool bNext)
	{
		UButton* Arrow = MakeButton(FLinearColor(0, 0, 0, 0), FLinearColor(0, 0, 0, 0), 10.0f);
		Style(Arrow, ButtonStyle(0.08f, 11.0f, 0.26f));
		if (bNext)
		{
			Arrow->OnClicked.AddDynamic(this, &UArenaSettingsWidget::HandleBuildTextureNext);
		}
		else
		{
			Arrow->OnClicked.AddDynamic(this, &UArenaSettingsWidget::HandleBuildTexturePrev);
		}
		UTextBlock* Glyph = MakeText(FText::FromString(bNext ? TEXT(">") : TEXT("<")), BodyFontFace, 12, Ink);
		if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(Arrow->SetContent(Glyph)))
		{
			ButtonSlot->SetPadding(FMargin(8.0f, 2.0f));
			ButtonSlot->SetHorizontalAlignment(HAlign_Center);
			ButtonSlot->SetVerticalAlignment(VAlign_Center);
		}
		Line->AddChildToHorizontalBox(Arrow)->SetVerticalAlignment(VAlign_Center);
	};

	MakeTextureArrow(false);
	USizeBox* ValueBox = WidgetTree->ConstructWidget<USizeBox>();
	ValueBox->SetWidthOverride(90.0f);
	BuildTextureValue = MakeText(FText::GetEmpty(), BodyFontFace, 15, Ink);
	ValueBox->SetContent(BuildTextureValue);
	Line->AddChildToHorizontalBox(ValueBox)->SetVerticalAlignment(VAlign_Center);
	MakeTextureArrow(true);
	List->AddChildToVerticalBox(Frame);
	RefreshBuildTextureValue();
}

void UArenaSettingsWidget::RefreshBuildTextureValue()
{
	if (BuildTextureValue)
	{
		BuildTextureValue->SetText(FText::AsNumber(GetBuildTextureIndex()));
	}
}

void UArenaSettingsWidget::HandleBuildTextureNext()
{
	SetBuildTextureIndex(GetBuildTextureIndex() % 4 + 1);
	RefreshBuildTextureValue();
}

void UArenaSettingsWidget::HandleBuildTexturePrev()
{
	SetBuildTextureIndex((GetBuildTextureIndex() + 2) % 4 + 1);
	RefreshBuildTextureValue();
}

void UArenaSettingsWidget::RefreshBuildPreviewColorSwatch()
{
	if (BuildPreviewColorSwatch)
	{
		BuildPreviewColorSwatch->SetBrush(FSlateRoundedBoxBrush(GetBuildPreviewColor(), 6.0f));
	}
}

void UArenaSettingsWidget::HandleBuildPreviewColorClicked()
{
	OpenGridColorPicker(true);
}

void UArenaSettingsWidget::OpenGridColorPicker(bool bBuildPreview)
{
	const FLinearColor InitialColor = bBuildPreview ? GetBuildPreviewColor() : GetEditGridColor();
	FColorPickerArgs Args(InitialColor, FOnLinearColorValueChanged::CreateLambda([this, bBuildPreview](FLinearColor NewColor)
	{
		NewColor.A = 1.0f;
		if (bBuildPreview)
		{
			SetBuildPreviewColor(NewColor);
			RefreshBuildPreviewColorSwatch();
		}
		else
		{
			SetEditGridColor(NewColor);
			RefreshEditGridColorSwatch();
		}
	}));
	Args.bUseAlpha = false;
	Args.bOnlyRefreshOnMouseUp = true;
	Args.bClampValue = true;
	Args.ParentWidget = TakeWidget();
	if (!OpenColorPicker(Args))
	{
		UE_LOG(LogTemp, Warning, TEXT("Could not open the edit-grid color picker."));
	}
}

void UArenaSettingsWidget::BuildLayout()
{
	using namespace ArenaSettings;
	using namespace ArenaGlass;
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("SettingsRoot"));
	WidgetTree->RootWidget = Root;

	auto Place = [Root](UWidget* Widget, const FAnchors& Anchors, const FMargin& Offsets, const FVector2D& Alignment, bool bAutoSize)
	{
		UCanvasPanelSlot* CanvasSlot = Root->AddChildToCanvas(Widget);
		CanvasSlot->SetAnchors(Anchors);
		CanvasSlot->SetOffsets(Offsets);
		CanvasSlot->SetAlignment(Alignment);
		CanvasSlot->SetAutoSize(bAutoSize);
	};

	// A glass panel: the blur of what is behind it and the translucent surface with its rim, in a rounded shape
	auto MakeGlassPanel = [this](const FMargin& InnerPadding, UBorder*& OutSurface)
	{
		UBackgroundBlur* Blur = WidgetTree->ConstructWidget<UBackgroundBlur>();
		Blur->SetBlurStrength(34.0f);
		Blur->SetApplyAlphaToBlur(false);
		Blur->SetCornerRadius(FVector4(11.0f, 11.0f, 11.0f, 11.0f));
		PanelBlurs.Add(Blur);
		OutSurface = WidgetTree->ConstructWidget<UBorder>();
		OutSurface->SetBrush(Surface(0.09f, 22.0f, 0.38f, FLinearColor::White, 1.3f));
		OutSurface->SetPadding(InnerPadding);
		Blur->SetContent(Layered(WidgetTree, OutSurface, 22.0f));
		return Blur;
	};

	// Backdrop: deep navy with big soft colored lights, the thing the glass refracts
	UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
	Background->SetBrush(FSlateColorBrush(FLinearColor(0.012f, 0.016f, 0.05f, 1.0f)));
	Place(Background, FAnchors(0, 0, 1, 1), FMargin(0), FVector2D::ZeroVector, false);
	auto AddLight = [&](const FLinearColor& Color, float X, float Y, float Size)
	{
		UBorder* Light = WidgetTree->ConstructWidget<UBorder>();
		Light->SetBrush(FSlateRoundedBoxBrush(Color, Size * 0.5f));
		Place(Light, FAnchors(X, Y), FMargin(0.0f, 0.0f, Size, Size), FVector2D(0.5f, 0.5f), false);
	};
	AddLight(FLinearColor(0.45f, 0.32f, 1.0f, 0.60f), 0.14f, 0.18f, 900.0f);
	AddLight(FLinearColor(1.0f, 0.40f, 0.40f, 0.40f), 0.94f, 0.55f, 700.0f);
	AddLight(FLinearColor(0.08f, 0.78f, 0.80f, 0.46f), 0.40f, 1.02f, 900.0f);
	AddLight(FLinearColor(0.20f, 0.42f, 1.0f, 0.40f), 0.82f, 0.02f, 600.0f);
	UBackgroundBlur* SoftLights = WidgetTree->ConstructWidget<UBackgroundBlur>();
	SoftLights->SetBlurStrength(90.0f);
	SoftLights->SetApplyAlphaToBlur(false);
	Place(SoftLights, FAnchors(0, 0, 1, 1), FMargin(0), FVector2D::ZeroVector, false);

	// Top category bar: a glass pill with small tabs
	UBorder* TopBar = WidgetTree->ConstructWidget<UBorder>();
	TopBar->SetBrush(Surface(0.22f, 22.0f, 0.22f, FLinearColor(0.0f, 0.0f, 0.0f, 1.0f)));
	TopBar->SetPadding(FMargin(3.0f));
	UHorizontalBox* Tabs = WidgetTree->ConstructWidget<UHorizontalBox>();
	TopBar->SetContent(Tabs);
	const FText TabNames[] = { LOCTEXT("TabVideo", "VÍDEO"), LOCTEXT("TabAudio", "AUDIO"), LOCTEXT("TabGame", "JUEGO"), LOCTEXT("TabControls", "CONTROLES"), LOCTEXT("TabAccount", "CUENTA") };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(TabNames); ++Index)
	{
		UArenaSettingRowHandler* TabHandler = NewObject<UArenaSettingRowHandler>(this);
		TabHandler->Row = Index;
		TabHandler->Owner = this;
		Handlers.Add(TabHandler);
		UButton* TabButton = MakeButton(FLinearColor(0, 0, 0, 0), FLinearColor(1, 1, 1, 0.05f), 17.0f);
		TabButton->OnClicked.AddDynamic(TabHandler, &UArenaSettingRowHandler::HandleTab);
		UBorder* Tab = WidgetTree->ConstructWidget<UBorder>();
		Tab->SetBrush(Index == 0 ? Surface(0.26f, 17.0f, 0.55f) : Surface(0.0f, 17.0f, 0.0f));
		Tab->SetPadding(FMargin(16.0f, 5.0f));
		UTextBlock* TabText = MakeText(TabNames[Index], BodyFontFace, 12, Index == 0 ? Ink : Dim);
		Tab->SetContent(TabText);
		TabButton->SetContent(Tab);
		TabBorders.Add(Tab);
		TabTexts.Add(TabText);
		Tabs->AddChildToHorizontalBox(TabButton)->SetPadding(FMargin(1.0f, 0.0f));
	}
	Place(TopBar, FAnchors(0.5f, 0.0f), FMargin(0.0f, 18.0f, 0.0f, 0.0f), FVector2D(0.5f, 0.0f), true);

	// Settings list
	UBorder* ListSurface = nullptr;
	UBackgroundBlur* ListPanel = MakeGlassPanel(FMargin(12.0f, 8.0f), ListSurface);
	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
	Scroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
	UVerticalBox* List = WidgetTree->ConstructWidget<UVerticalBox>();
	Scroll->AddChild(List);
	ListScroll = Scroll;
	VideoList = List;
	ControlsList = WidgetTree->ConstructWidget<UVerticalBox>();
	SoonList = WidgetTree->ConstructWidget<UVerticalBox>();
	SoonList->AddChildToVerticalBox(MakeText(LOCTEXT("Soon", "Esta sección llegará pronto."), BodyFontFace, 14, Dim))->SetPadding(FMargin(14.0f, 24.0f));
	ListSurface->SetContent(Scroll);
	Place(ListPanel, FAnchors(0.0f, 0.0f, 0.60f, 1.0f), FMargin(70.0f, 66.0f, 10.0f, 60.0f), FVector2D::ZeroVector, false);

	AddSection(List, LOCTEXT("SecDisplay", "PANTALLA"));
	{ const int32 Ids[] = { WindowMode, Resolution, VSync, FrameLimit, RenderMode }; for (const int32 Id : Ids) { AddRow(List, Id); } }
	AddSection(List, LOCTEXT("SecGraphics", "GRÁFICOS"));
	{ const int32 Ids[] = { Brightness, UIScale, ColorBlind, ColorBlindStrength, MotionBlur }; for (const int32 Id : Ids) { AddRow(List, Id); } }
	AddSection(List, LOCTEXT("SecQuality", "CALIDAD GRÁFICA"));
	{ const int32 Ids[] = { AutoAdjust, Preset, AntiAliasing, TemporalSR, NaniteGeometry, GlobalIllumination, Reflections, Shadows, ViewDistance, Textures, Effects, PostProcess }; for (const int32 Id : Ids) { AddRow(List, Id); } }
	AddSection(List, LOCTEXT("SecAdvanced", "GRÁFICOS AVANZADOS"));
	AddRow(List, ShowFps);
	AddSection(List, LOCTEXT("SecBuild", "CONSTRUCCIÓN"));
	AddEditGridColorRow(List);
	AddBuildPreviewColorRow(List);
	AddBuildTextureRow(List);

	// Controls tab: one row per key the player can change
	{
		FText LastGroup;
		for (int32 Index = 0; Index < static_cast<int32>(ArenaControls::EAction::Count); ++Index)
		{
			const ArenaControls::FActionInfo& Info = ArenaControls::Info(static_cast<ArenaControls::EAction>(Index));
			if (!LastGroup.EqualTo(Info.Group))
			{
				AddSection(ControlsList, Info.Group);
				LastGroup = Info.Group;
			}
			AddKeyRow(ControlsList, Index);
		}
	}

	// Description panel on the right
	UBorder* DescSurface = nullptr;
	UBackgroundBlur* DescPanel = MakeGlassPanel(FMargin(22.0f, 18.0f), DescSurface);
	UVerticalBox* Desc = WidgetTree->ConstructWidget<UVerticalBox>();
	DescTitle = MakeText(FText::GetEmpty(), BodyFontFace, 20, Ink);
	DescTitle->SetAutoWrapText(true);
	Desc->AddChildToVerticalBox(DescTitle)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
	DescBody = MakeText(FText::GetEmpty(), BodyFontFace, 14, Dim);
	DescBody->SetAutoWrapText(true);
	Desc->AddChildToVerticalBox(DescBody);
	DescOptions = WidgetTree->ConstructWidget<UVerticalBox>();
	Desc->AddChildToVerticalBox(DescOptions)->SetPadding(FMargin(0.0f, 20.0f, 0.0f, 0.0f));
	DescSurface->SetContent(Desc);
	Place(DescPanel, FAnchors(0.61f, 0.0f, 1.0f, 1.0f), FMargin(0.0f, 66.0f, 70.0f, 60.0f), FVector2D::ZeroVector, false);

	// Bottom buttons: small glass pills
	UHorizontalBox* Bottom = WidgetTree->ConstructWidget<UHorizontalBox>();
	auto AddBottomButton = [this, Bottom](const FText& Key, const FText& Label, const FLinearColor& Tint, bool bReset)
	{
		UButton* Button = MakeButton(FLinearColor(0, 0, 0, 0), FLinearColor(0, 0, 0, 0), 12.0f);
		ArenaGlass::Style(Button, ArenaGlass::ButtonStyle(0.14f, 15.0f, 0.45f, Tint));
		UHorizontalBox* Content = WidgetTree->ConstructWidget<UHorizontalBox>();
		UBorder* KeyCap = WidgetTree->ConstructWidget<UBorder>();
		KeyCap->SetBrush(ArenaGlass::Surface(0.30f, 6.0f, 0.20f, FLinearColor(0.0f, 0.0f, 0.0f, 1.0f)));
		KeyCap->SetPadding(FMargin(6.0f, 1.0f));
		KeyCap->SetContent(MakeText(Key, BodyFontFace, 10, ArenaGlass::Ink));
		Content->AddChildToHorizontalBox(KeyCap)->SetVerticalAlignment(VAlign_Center);
		UHorizontalBoxSlot* LabelSlot = Content->AddChildToHorizontalBox(MakeText(Label, BodyFontFace, 12, ArenaGlass::Ink));
		LabelSlot->SetPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f));
		LabelSlot->SetVerticalAlignment(VAlign_Center);
		if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(Button->SetContent(Content)))
		{
			ButtonSlot->SetPadding(FMargin(12.0f, 5.0f));
		}
		if (bReset)
		{
			Button->OnClicked.AddDynamic(this, &UArenaSettingsWidget::HandleResetClicked);
		}
		else
		{
			Button->OnClicked.AddDynamic(this, &UArenaSettingsWidget::HandleBackClicked);
		}
		Bottom->AddChildToHorizontalBox(Button)->SetPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f));
	};
	AddBottomButton(FText::FromString(TEXT("R")), LOCTEXT("Reset", "RESTABLECER VALORES PREDETERMINADOS"), Violet, true);
	AddBottomButton(FText::FromString(TEXT("ESC")), LOCTEXT("Back", "ATRÁS"), Ice, false);
	Place(Bottom, FAnchors(1.0f, 1.0f), FMargin(-70.0f, -16.0f, 0.0f, 0.0f), FVector2D(1.0f, 1.0f), true);

	FpsText = MakeText(FText::GetEmpty(), BodyFontFace, 11, Dim);
	Place(FpsText, FAnchors(0.0f, 1.0f), FMargin(80.0f, -26.0f, 0.0f, 0.0f), FVector2D(0.0f, 1.0f), true);

	HoveredRow = 0;
	for (int32 Row = 0; Row < RowSettings.Num(); ++Row)
	{
		RefreshRow(Row);
	}
	RefreshDescription();
}

void UArenaSettingsWidget::AddKeyRow(UVerticalBox* List, int32 ActionIndex)
{
	const ArenaControls::FActionInfo& Info = ArenaControls::Info(static_cast<ArenaControls::EAction>(ActionIndex));
	UArenaSettingRowHandler* Handler = NewObject<UArenaSettingRowHandler>(this);
	Handler->Row = ActionIndex;
	Handler->Owner = this;
	Handlers.Add(Handler);

	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>();
	Frame->SetPadding(FMargin(14.0f, 4.0f, 10.0f, 4.0f));
	Frame->SetBrush(ArenaGlass::Surface(0.0f, 14.0f, 0.0f));
	UButton* RowButton = MakeButton(FLinearColor(0, 0, 0, 0), FLinearColor(1, 1, 1, 0.04f), 12.0f);
	RowButton->OnClicked.AddDynamic(Handler, &UArenaSettingRowHandler::HandleKeyRow);
	Frame->SetContent(RowButton);

	UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (UButtonSlot* RowSlot = Cast<UButtonSlot>(RowButton->SetContent(Line)))
	{
		RowSlot->SetHorizontalAlignment(HAlign_Fill);
		RowSlot->SetVerticalAlignment(VAlign_Center);
		RowSlot->SetPadding(FMargin(4.0f, 4.0f));
	}
	UHorizontalBoxSlot* LabelSlot = Line->AddChildToHorizontalBox(MakeText(Info.Label, BodyFontFace, 15, ArenaGlass::Ink));
	LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	LabelSlot->SetVerticalAlignment(VAlign_Center);

	// the key shown as a small rounded cap
	UBorder* Cap = WidgetTree->ConstructWidget<UBorder>();
	Cap->SetBrush(ArenaGlass::Surface(0.26f, 9.0f, 0.5f, ArenaGlass::Ice));
	Cap->SetPadding(FMargin(14.0f, 4.0f));
	USizeBox* CapBox = WidgetTree->ConstructWidget<USizeBox>();
	CapBox->SetMinDesiredWidth(120.0f);
	UTextBlock* KeyText = MakeText(ArenaControls::KeyName(ArenaControls::Get(static_cast<ArenaControls::EAction>(ActionIndex))), BodyFontFace, 13, ArenaGlass::Ink);
	KeyText->SetJustification(ETextJustify::Center);
	CapBox->SetContent(KeyText);
	Cap->SetContent(CapBox);
	Line->AddChildToHorizontalBox(Cap)->SetVerticalAlignment(VAlign_Center);

	if (KeyTexts.Num() <= ActionIndex)
	{
		KeyTexts.SetNum(ActionIndex + 1);
		KeyCaps.SetNum(ActionIndex + 1);
	}
	KeyTexts[ActionIndex] = KeyText;
	KeyCaps[ActionIndex] = Cap;
	List->AddChildToVerticalBox(Frame);
}

void UArenaSettingsWidget::RefreshKeys()
{
	for (int32 Index = 0; Index < KeyTexts.Num(); ++Index)
	{
		if (!KeyTexts[Index])
		{
			continue;
		}
		const bool bCapturing = Index == CaptureAction;
		KeyTexts[Index]->SetText(bCapturing ? LOCTEXT("PressKey", "PRESIONA UNA TECLA…") : ArenaControls::KeyName(ArenaControls::Get(static_cast<ArenaControls::EAction>(Index))));
		if (KeyCaps[Index])
		{
			KeyCaps[Index]->SetBrush(bCapturing ? ArenaGlass::Surface(0.30f, 9.0f, 0.9f, ArenaGlass::Mint) : ArenaGlass::Surface(0.26f, 9.0f, 0.5f, ArenaGlass::Ice));
		}
	}
}

void UArenaSettingsWidget::BeginCapture(int32 ActionIndex)
{
	CaptureAction = ActionIndex;
	RefreshKeys();
	RefreshDescription();
}

void UArenaSettingsWidget::ShowTab(int32 Tab)
{
	using namespace ArenaGlass;
	if (!ListScroll)
	{
		return;
	}
	ActiveTab = Tab;
	CaptureAction = INDEX_NONE;
	ListScroll->ClearChildren();
	ListScroll->AddChild(Tab == 0 ? VideoList.Get() : (Tab == 3 ? ControlsList.Get() : SoonList.Get()));
	for (int32 Index = 0; Index < TabBorders.Num(); ++Index)
	{
		TabBorders[Index]->SetBrush(Index == Tab ? Surface(0.26f, 17.0f, 0.55f) : Surface(0.0f, 17.0f, 0.0f));
		TabTexts[Index]->SetColorAndOpacity(FSlateColor(Index == Tab ? Ink : Dim));
	}
	RefreshKeys();
	RefreshDescription();
}

void UArenaSettingsWidget::RefreshRow(int32 Row)
{
	using namespace ArenaSettings;
	if (!RowWidgets.IsValidIndex(Row))
	{
		return;
	}
	const int32 Id = RowSettings[Row];
	FRowWidgets& Widgets = RowWidgets[Row];
	if (Widgets.Frame)
	{
		const bool bHovered = Row == HoveredRow;
		Widgets.Frame->SetBrush(bHovered
			? ArenaGlass::Surface(0.12f, 14.0f, 0.42f, ArenaGlass::Ice)
			: ArenaGlass::Surface(0.0f, 14.0f, 0.0f));
	}
	if (Widgets.Value == nullptr)
	{
		return;
	}
	const int32 Value = Get(Id);
	Widgets.Value->SetText(Id == Resolution ? ResolutionText(GetResolutions()[Value]) : Def(Id).Options[Value]);
	for (int32 Index = 0; Index < Widgets.Segments.Num(); ++Index)
	{
		Widgets.Segments[Index]->SetBrushColor(Index == Value ? ArenaGlass::Mint : FLinearColor(1.0f, 1.0f, 1.0f, 0.20f));
	}
}

void UArenaSettingsWidget::RefreshDescription()
{
	using namespace ArenaSettings;
	if (DescTitle && ActiveTab != 0)
	{
		DescOptions->ClearChildren();
		if (ActiveTab == 3)
		{
			DescTitle->SetText(LOCTEXT("ControlsTitle", "Controles"));
			DescBody->SetText(CaptureAction == INDEX_NONE
				? LOCTEXT("ControlsBody", "Haz clic en una acción y pulsa la tecla nueva. Si ya la usa otra acción, las dos se intercambian. Escape cancela. R restablece todo. Las teclas de construcción se aplican al instante.")
				: LOCTEXT("ControlsCapture", "Pulsa la tecla o el botón del ratón que quieres usar. Escape cancela."));
		}
		else
		{
			DescTitle->SetText(LOCTEXT("SoonTitle", "Próximamente"));
			DescBody->SetText(FText::GetEmpty());
		}
		return;
	}
	if (!RowSettings.IsValidIndex(HoveredRow) || DescTitle == nullptr)
	{
		return;
	}
	const int32 Id = RowSettings[HoveredRow];
	DescTitle->SetText(Def(Id).Label);
	DescBody->SetText(Def(Id).Description);
	DescOptions->ClearChildren();
	if (Def(Id).bButton || NumOptions(Id) > 8)
	{
		return;
	}
	const int32 Value = Get(Id);
	for (int32 Index = 0; Index < NumOptions(Id); ++Index)
	{
		const FText Option = Id == Resolution ? ResolutionText(GetResolutions()[Index]) : Def(Id).Options[Index];
		UBorder* Line = WidgetTree->ConstructWidget<UBorder>();
		Line->SetPadding(FMargin(12.0f, 3.0f));
		const bool bSelected = Index == Value;
		Line->SetBrush(bSelected ? ArenaGlass::Surface(0.20f, 12.0f, 0.55f, ArenaGlass::Mint) : ArenaGlass::Surface(0.0f, 12.0f, 0.0f));
		Line->SetContent(MakeText(FText::FromString(Option.ToString().ToUpper()), BodyFontFace, 12,
			bSelected ? ArenaGlass::Ink : ArenaGlass::Dim));
		UVerticalBoxSlot* LineSlot = DescOptions->AddChildToVerticalBox(Line);
		LineSlot->SetHorizontalAlignment(HAlign_Left);
		LineSlot->SetPadding(FMargin(0.0f, 2.0f));
	}
}

FReply UArenaSettingsWidget::HandleKey(const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (CaptureAction != INDEX_NONE)
	{
		if (Key != EKeys::Escape && Key.IsValid() && !Key.IsGamepadKey())
		{
			ArenaControls::Set(static_cast<ArenaControls::EAction>(CaptureAction), Key);
		}
		CaptureAction = INDEX_NONE;
		RefreshKeys();
		RefreshDescription();
		return FReply::Handled();      // Escape only cancels the capture, it does not close the screen
	}
	if (Key == EKeys::Escape || Key == EKeys::BackSpace || Key == EKeys::Gamepad_FaceButton_Right)
	{
		Close();
		return FReply::Handled();
	}
	if (Key == EKeys::R)
	{
		ResetToDefaults();
		return FReply::Handled();
	}
	if (ActiveTab != 0)
	{
		return FReply::Unhandled();
	}
	if (Key == EKeys::Down || Key == EKeys::Up)
	{
		HoverRow(FMath::Clamp(HoveredRow + (Key == EKeys::Down ? 1 : -1), 0, RowSettings.Num() - 1));
		return FReply::Handled();
	}
	if (Key == EKeys::Left || Key == EKeys::Right)
	{
		StepRow(HoveredRow, Key == EKeys::Right ? 1 : -1);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply UArenaSettingsWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FReply Reply = HandleKey(InKeyEvent);
	return Reply.IsEventHandled() ? Reply : Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply UArenaSettingsWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FReply Reply = HandleKey(InKeyEvent);
	return Reply.IsEventHandled() ? Reply : Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

FReply UArenaSettingsWidget::NativeOnPreviewMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	const FKey Button = InMouseEvent.GetEffectingButton();
	if (CaptureAction != INDEX_NONE && Button != EKeys::LeftMouseButton)
	{
		ArenaControls::Set(static_cast<ArenaControls::EAction>(CaptureAction), Button);
		CaptureAction = INDEX_NONE;
		RefreshKeys();
		RefreshDescription();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewMouseButtonDown(InGeometry, InMouseEvent);
}

void UArenaSettingsWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// Same units as the border brush: the blur applies the UI scale to the radius itself
	const float Radius = ArenaGlass::BlurRadius(); // SBackgroundBlur multiplies the radius by the widget scale itself, like the border brush
	if (!FMath::IsNearlyEqual(Radius, BlurScale, 0.01f))
	{
		BlurScale = Radius;
		for (UBackgroundBlur* Blur : PanelBlurs)
		{
			if (Blur) { Blur->SetCornerRadius(FVector4(Radius, Radius, Radius, Radius)); }
		}
	}

	FpsAccum += InDeltaTime;
	++FpsFrames;
	if (FpsAccum >= 0.5f && FpsText)
	{
		FpsText->SetText(FText::FromString(FString::Printf(TEXT("%d FPS"), FMath::RoundToInt(FpsFrames / FpsAccum))));
		FpsAccum = 0.0f;
		FpsFrames = 0;
	}
}

void UArenaSettingsWidget::HandleResetClicked() { ResetToDefaults(); }
void UArenaSettingsWidget::HandleBackClicked() { Close(); }

#undef LOCTEXT_NAMESPACE

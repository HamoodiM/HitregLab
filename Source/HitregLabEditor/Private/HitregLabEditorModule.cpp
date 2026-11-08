// Hitreg Lab editor module: the Aim Assist Tuner tab (Tools -> Hitreg Lab -> Aim Assist Tuner, or the
// console command HitregLab.OpenAimTuner). Edits the aim assist data asset live (also during PIE: the asset
// is shared), previews the stick response curve and runs the bot-thumb benchmark.
#include "Modules/ModuleManager.h"
#include "Aim/HLAimAssist.h"
#include "Aim/HLAimAssistSettings.h"
#include "Core/HLGameMode.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "IDetailsView.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "PropertyEditorModule.h"
#include "Rendering/DrawElements.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "HitregLabEditor"

namespace HitregLabEditor
{
	static const FName AimTunerTabId(TEXT("HitregLabAimTuner"));

	UHLAimAssistSettings* LoadSettings()
	{
		return Cast<UHLAimAssistSettings>(GetDefault<AHLGameMode>()->AimAssistSettings.TryLoad());
	}
}

/** Plots the stick response (deflection -> look rate) and the slowdown falloff across the bubble. */
class SAimCurvePreview : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SAimCurvePreview) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UHLAimAssistSettings>, Settings)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Settings = InArgs._Settings;
	}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(420.f, 220.f); }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Clip, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override
	{
		const UHLAimAssistSettings* S = Settings.Get();
		const FVector2D Size = Geo.GetLocalSize();
		const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
		FSlateDrawElement::MakeBox(Out, Layer, Geo.ToPaintGeometry(), White, ESlateDrawEffect::None, FLinearColor(0.02f, 0.02f, 0.025f));
		if (!S)
		{
			return Layer + 1;
		}
		const float Pad = 18.f;
		const float W = (Size.X - Pad * 3.f) * 0.5f;
		const float H = Size.Y - Pad * 2.f;
		auto Plot = [&](float X0, TFunctionRef<float(float)> F, const FLinearColor& C)
		{
			TArray<FVector2D> Pts;
			for (int32 i = 0; i <= 64; ++i)
			{
				const float T = i / 64.f;
				Pts.Add(FVector2D(X0 + T * W, Pad + H - FMath::Clamp(F(T), 0.f, 1.f) * H));
			}
			FSlateDrawElement::MakeLines(Out, Layer + 1, Geo.ToPaintGeometry(), Pts, ESlateDrawEffect::None, C, true, 2.f);
			TArray<FVector2D> Axis = { FVector2D(X0, Pad), FVector2D(X0, Pad + H), FVector2D(X0 + W, Pad + H) };
			FSlateDrawElement::MakeLines(Out, Layer + 1, Geo.ToPaintGeometry(), Axis, ESlateDrawEffect::None, FLinearColor(0.4f, 0.4f, 0.45f), true, 1.f);
		};
		// Left: stick deflection -> fraction of max look rate
		Plot(Pad, [S](float T) { return HLAimAssist::ApplyStickCurve(*S, FVector2D(T, 0.f)).X; }, FLinearColor(0.4f, 0.85f, 1.f));
		// Right: look-rate multiplier from the bubble centre (left) to its edge (right)
		Plot(Pad * 2.f + W, [S](float T) { return FMath::Lerp(S->SlowdownMinScale, 1.f, FMath::SmoothStep(0.f, 1.f, T)); }, FLinearColor(1.f, 0.7f, 0.3f));
		return Layer + 2;
	}

private:
	TWeakObjectPtr<UHLAimAssistSettings> Settings;
};

class FHitregLabEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(HitregLabEditor::AimTunerTabId, FOnSpawnTab::CreateRaw(this, &FHitregLabEditorModule::SpawnTuner))
			.SetDisplayName(LOCTEXT("TunerTitle", "Aim Assist Tuner"))
			.SetTooltipText(LOCTEXT("TunerTip", "Tune controller look and aim assist live, and benchmark it"))
			.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());

		OpenCommand = IConsoleManager::Get().RegisterConsoleCommand(TEXT("HitregLab.OpenAimTuner"), TEXT("Opens the Aim Assist Tuner tab"),
			FConsoleCommandDelegate::CreateLambda([]() { FGlobalTabmanager::Get()->TryInvokeTab(HitregLabEditor::AimTunerTabId); }));
	}

	virtual void ShutdownModule() override
	{
		if (OpenCommand)
		{
			IConsoleManager::Get().UnregisterConsoleObject(OpenCommand);
		}
		if (FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(HitregLabEditor::AimTunerTabId);
		}
	}

private:
	TSharedRef<SDockTab> SpawnTuner(const FSpawnTabArgs& Args)
	{
		UHLAimAssistSettings* Settings = HitregLabEditor::LoadSettings();
		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		FDetailsViewArgs ViewArgs;
		ViewArgs.bHideSelectionTip = true;
		ViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		TSharedRef<IDetailsView> Details = PropertyEditor.CreateDetailView(ViewArgs);
		Details->SetObject(Settings);

		TSharedRef<STextBlock> Result = SNew(STextBlock)
			.AutoWrapText(true)
			.Text(Settings ? LOCTEXT("Ready", "Edit values on the left; they apply live, even in PIE. Run the benchmark to measure them.")
				: LOCTEXT("NoAsset", "No aim assist asset found at the path in DefaultGame.ini (AimAssistSettings)."));
		TWeakObjectPtr<UHLAimAssistSettings> WeakSettings = Settings;

		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SSplitter)
				+ SSplitter::Slot().Value(0.55f)
				[
					Details
				]
				+ SSplitter::Slot().Value(0.45f)
				[
					SNew(SBorder).Padding(10.f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
						[
							SNew(STextBlock).Text(LOCTEXT("CurveLabel", "Left: stick deflection -> look rate.   Right: slowdown from target centre to bubble edge."))
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SAimCurvePreview).Settings(WeakSettings)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 6)
						[
							SNew(SButton)
							.Text(LOCTEXT("Bench", "Run bot-thumb benchmark (50 trials per mode)"))
							.OnClicked_Lambda([WeakSettings, Result]()
							{
								if (UHLAimAssistSettings* S = WeakSettings.Get())
								{
									const FString Dir = FPaths::ProjectSavedDir() / TEXT("Measure");
									IFileManager::Get().MakeDirectory(*Dir, true);
									Result->SetText(FText::FromString(HLAimBench::RunToCsv(*S, 50, Dir)));
								}
								return FReply::Handled();
							})
						]
						+ SVerticalBox::Slot().FillHeight(1.f)
						[
							Result
						]
					]
				]
			];
	}

	IConsoleObject* OpenCommand = nullptr;
};

IMPLEMENT_MODULE(FHitregLabEditorModule, HitregLabEditor)

#undef LOCTEXT_NAMESPACE

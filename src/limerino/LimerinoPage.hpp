#pragma once

#include "widgets/settingspages/SettingsPage.hpp"

namespace chatterino {

class DescriptionLabel;
class GeneralPageView;

class LimerinoPage : public SettingsPage
{
    Q_OBJECT

public:
    LimerinoPage();

    bool filterElements(const QString &query) override;
    void onShow() override;

private:
    void initLayout(GeneralPageView &layout);
    void rebuildAuthSummary();
    void rebuildPubSubDiagnostics();

    GeneralPageView *view_{};
    DescriptionLabel *authSummaryLabel_{};
    DescriptionLabel *pubsubSummaryLabel_{};
    DescriptionLabel *pubsubDetailLabel_{};
};

}  // namespace chatterino

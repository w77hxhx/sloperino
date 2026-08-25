// SPDX-License-Identifier: MIT
// Anti-drift tests for the Limerino feature catalog: the settings wiki renders
// purely from limerinoFeatures(), so this suite is what stops the wiki lying
// again.

#include "limerino/LimerinoFeatures.hpp"

#include "controllers/commands/CommandController.hpp"
#include "mocks/BaseApplication.hpp"
#include "providers/limerino/commands/LimerinoCommands.hpp"
#include "Test.hpp"

#include <gtest/gtest.h>
#include <QSet>

using namespace chatterino;
using namespace chatterino::limerino;

namespace {

class MockApplication : public mock::BaseApplication
{
public:
    MockApplication()
        : commands(this->paths_)
    {
    }

    CommandController *getCommands() override
    {
        return &this->commands;
    }

    CommandController commands;
};

}  // namespace

TEST(LimerinoFeatures, IdsAreUnique)
{
    QSet<QString> seen;
    for (const auto &f : limerinoFeatures())
    {
        EXPECT_FALSE(f.id.isEmpty()) << "feature with empty id: " << f.name;
        EXPECT_TRUE(seen.insert(f.id).second)
            << "duplicate feature id: " << f.id;
    }
}

TEST(LimerinoFeatures, EveryEntryHasAccessPaths)
{
    // A feature is a capability; a command is one way to reach it. Requiring
    // at least one access path is what stops the catalog collapsing back into
    // a command list.
    for (const auto &f : limerinoFeatures())
    {
        EXPECT_FALSE(f.access.isEmpty())
            << "feature with no access path: " << f.id;
        for (const auto &a : f.access)
        {
            EXPECT_FALSE(a.detail.isEmpty())
                << "empty access detail: " << f.id;
        }
    }
}

TEST(LimerinoFeatures, ProseIsNonEmpty)
{
    for (const auto &f : limerinoFeatures())
    {
        EXPECT_FALSE(f.name.trimmed().isEmpty()) << f.id;
        EXPECT_FALSE(f.category.trimmed().isEmpty()) << f.id;
        EXPECT_FALSE(f.summary.trimmed().isEmpty()) << f.id;
        EXPECT_FALSE(f.description.trimmed().isEmpty()) << f.id;
    }
}

TEST(LimerinoFeatures, EveryCatalogCommandIsRegisteredAndViceVersa)
{
    // Membership must hold in BOTH directions between the catalog and what
    // initialize() actually registers. The registered delta is computed by
    // diffing the autocomplete command list around initialize(), so upstream
    // commands never enter the comparison at all.
    MockApplication app;
    auto &controller = *app.getCommands();

    const QStringList before = controller.getDefaultChatterinoCommandList();
    LimerinoCommands::initialize(controller);
    const QStringList after = controller.getDefaultChatterinoCommandList();

    const QSet<QString> beforeSet(before.begin(), before.end());
    QSet<QString> registered;
    for (const auto &cmd : after)
    {
        if (!beforeSet.contains(cmd))
        {
            registered.insert(cmd);
        }
    }

    QSet<QString> catalogCommands;
    for (const auto &f : limerinoFeatures())
    {
        for (const auto &cmd : f.commands)
        {
            catalogCommands.insert(cmd);
        }
    }

    for (const auto &cmd : catalogCommands)
    {
        EXPECT_TRUE(registered.contains(cmd))
            << "catalog command not actually registered: " << cmd;
    }
    for (const auto &cmd : registered)
    {
        EXPECT_TRUE(catalogCommands.contains(cmd))
            << "registered command missing from the catalog: " << cmd;
    }
}

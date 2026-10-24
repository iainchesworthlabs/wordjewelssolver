// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import WordJewelsSolver
import WordJewelsSolver.Parent

// The size matrix over Home, My jewels and the parent area's screens (the gate,
// the dashboard, the settings, About and Licences): 900x620, 900x1200, 1024x768,
// 2400x1200 and 150% text. Every control that takes Tab focus is at least 48 dp
// each way, wholly inside the window and the Flickable that holds it, carries no
// truncated label and sits on no other control. The screens read the demo data
// of the suites' own fixture.
SizeMatrixCase {
    name: "SizeMatrixScreens"
    subjects: [homeComponent, jewelsComponent, gateComponent, dashboardComponent,
               settingsComponent, aboutComponent, licencesComponent]

    Component { id: homeComponent; Home { controller: parentController } }
    Component { id: jewelsComponent; JewelsScreen { controller: parentController } }
    Component { id: gateComponent; ParentArea { controller: parentController } }
    Component { id: dashboardComponent; DashboardScreen { controller: parentController } }
    Component { id: settingsComponent; SettingsScreen { controller: parentController } }
    Component { id: aboutComponent; AboutScreen { controller: parentController } }
    Component { id: licencesComponent; LicencesScreen { controller: parentController } }
}

-- Copyright © 2008-2019 Pioneer Developers. See AUTHORS.txt for details
-- Licensed under the terms of the GPL v3. See licenses/GPL-3.txt

local Engine = import("Engine")
local Lang = import("Lang")
local Game = import("Game")
local Equipment = import("Equipment")
local ShipDef = import("ShipDef")

local SmallLabeledButton = import("ui/SmallLabeledButton")
local InfoGauge = import("ui/InfoGauge")

local ui = Engine.ui
local l = Lang.GetResource("ui-core");

local econTrade = function ()

	local cash = Game.player:GetMoney()

	local player = Game.player

	local totalCabins = Game.player:GetEquipCountOccupied("cabin")
	local usedCabins = totalCabins - (Game.player.cabin_cap or 0)

	-- Using econTrade as an enclosure for the functions attached to the
	-- buttons in the UI object that it returns. Seems like the most sane
	-- way to handle it; hopefully the enclosure will evaporate shortly
	-- after the UI is disposed of.

	-- Make a cargo list widget that we can revisit and update
	local cargoListWidget = ui:Margin(0)

	function updateCargoListWidget ()

		local cargoNameColumn = {}
		local cargoQuantityColumn = {}
		local cargoJettisonColumn = {}

		local count = {}
		for k,et in pairs(Game.player:GetEquip("cargo")) do
			if not count[et] then count[et] = 0 end
			count[et] = count[et]+1
		end
		for et,nb in pairs(count) do
			table.insert(cargoNameColumn, ui:Label(et:GetName()))
			table.insert(cargoQuantityColumn, ui:Label(nb.."t"))

			local jettisonButton = SmallLabeledButton.New(l.JETTISON)
			jettisonButton.button.onClick:Connect(function ()
				Game.player:Jettison(et)
				updateCargoListWidget()
				cargoListWidget:SetInnerWidget(updateCargoListWidget())
			end)
			if Game.player.flightState ~= "FLYING" then
				jettisonButton.widget:Disable()
			end
			table.insert(cargoJettisonColumn, jettisonButton.widget)
		end

		-- Function returns a UI with which to populate the cargo list widget
		return
			ui:VBox(10):PackEnd({
				ui:Label(l.CARGO):SetFont("HEADING_LARGE"),
				ui:Scroller():SetInnerWidget(
					ui:Grid(3,1)
						:SetColumn(0, { ui:VBox():PackEnd(cargoNameColumn) })
						:SetColumn(1, { ui:VBox():PackEnd(cargoQuantityColumn) })
						:SetColumn(2, { ui:VBox():PackEnd(cargoJettisonColumn) })
				)
			})
	end

	cargoListWidget:SetInnerWidget(updateCargoListWidget())

	local cargoGauge = ui:Gauge()
	local cargoUsedLabel = ui:Label("")
	local cargoFreeLabel = ui:Label("")
	local function cargoUpdate ()
		cargoGauge:SetUpperValue(player.totalCargo)
		cargoGauge:SetValue(player.usedCargo)
		cargoUsedLabel:SetText(string.interp(l.CARGO_T_USED, { amount = player.usedCargo }))
		cargoFreeLabel:SetText(string.interp(l.CARGO_T_FREE, { amount = player.totalCargo-player.usedCargo }))
	end
	player:Connect("usedCargo", cargoUpdate)
	player:Connect("totalCargo", cargoUpdate)
	cargoUpdate()

	local fuelGauge = InfoGauge.New({
		label          = ui:NumberLabel("PERCENT"),
		warningLevel   = 0.1,
		criticalLevel  = 0.05,
		levelAscending = false,
	})
	fuelGauge.label:Bind("valuePercent", Game.player, "fuel")
	fuelGauge.gauge:Bind("valuePercent", Game.player, "fuel")

	-- Define the refuel button
	local refuelOne = ui:Button("+1")
	local refuelTen = ui:Button("+10")
	local refuel100 = ui:Button("+100")
	local pumpDownOne = ui:Button("-1")
	local pumpDownTen = ui:Button("-10")
	local pumpDown100 = ui:Button("-100")

	local refuelButtonRefresh = function ()
		if Game.player.fuel == 100 or Game.player:CountEquip(Equipment.cargo.hydrogen) == 0 then
			refuelOne:Disable()
			refuelTen:Disable()
			refuel100:Disable()
		else
			refuelOne:Enable()
			refuelTen:Enable()
			refuel100:Enable()
		end
		if Game.player.fuel == 0 or Game.player:GetEquipFree("cargo") == 0 then
			pumpDownOne:Disable()
			pumpDownTen:Disable()
			pumpDown100:Disable()
		else
			pumpDownOne:Enable()
			pumpDownTen:Enable()
			pumpDown100:Enable()
		end
		local fuel_percent = Game.player.fuel/100
		fuelGauge.gauge:SetValue(fuel_percent)
		fuelGauge.label:SetValue(fuel_percent)
	end
	refuelButtonRefresh()

	local refuel = function (fuel)
		-- UI button where the player clicks to refuel...
		Game.player:Refuel(fuel)
		-- ...then we update the cargo list widget...
		cargoListWidget:SetInnerWidget(updateCargoListWidget())

		refuelButtonRefresh()
	end

	local pumpDown = function (fuel)
		local fuelTankMass = ShipDef[Game.player.shipId].fuelTankMass
		local availableFuel = math.floor(Game.player.fuel / 100 * fuelTankMass)
		if fuel > availableFuel then fuel = availableFuel end
		local drainedFuel = Game.player:AddEquip(Equipment.cargo.hydrogen, fuel)
		Game.player:SetFuelPercent(math.clamp(Game.player.fuel - drainedFuel * 100 / fuelTankMass, 0, 100))
		cargoListWidget:SetInnerWidget(updateCargoListWidget())

		refuelButtonRefresh()
	end

	refuelOne.onClick:Connect(function () refuel(1) end)
	refuelTen.onClick:Connect(function () refuel(10) end)
	refuel100.onClick:Connect(function () refuel(100) end)
	pumpDownOne.onClick:Connect(function () pumpDown(1) end)
	pumpDownTen.onClick:Connect(function () pumpDown(10) end)
	pumpDown100.onClick:Connect(function () pumpDown(100) end)

	return ui:Expand():SetInnerWidget(
		ui:Grid({48,4,48},1)
			:SetColumn(0, {
				ui:Margin(5, "HORIZONTAL",
					ui:VBox(20):PackEnd({
						ui:Grid(2,1)
							:SetColumn(0, {
								ui:VBox():PackEnd({
									ui:Label(l.CASH..":"),
									ui:Margin(10),
									ui:Label(l.CARGO_SPACE..":"),
									ui:Margin(5),
									ui:Label(l.CABINS..":"),
									ui:Margin(10),
								})
							})
							:SetColumn(1, {
								ui:VBox():PackEnd({
									ui:Label(string.format("$%.2f", cash)),
									ui:Margin(10),
									ui:Margin(0, "HORIZONTAL",
										ui:HBox(10):PackEnd({
											ui:Align("MIDDLE",
												ui:HBox(10):PackEnd({
													cargoGauge,
												})
											),
											ui:VBox():PackEnd({
												cargoUsedLabel,
												cargoFreeLabel,
											}):SetFont("XSMALL"),
										})
									),
									ui:Grid(2,1):SetRow(0, { ui:Label(l.TOTAL..totalCabins), ui:Label(l.USED..": "..usedCabins) }),
									ui:Margin(10),
								})
							}),
						ui:Grid({50,10,40},1)
							:SetRow(0, {
								ui:HBox(5):PackEnd({
									ui:Label(l.FUEL..":"),
									fuelGauge,
								}),
								nil,
								ui:VBox(5):PackEnd({
									ui:Label(l.REFUEL),
									ui:HBox(5):PackEnd({
										refuelOne,
										refuelTen,
										refuel100,
									}):SetFont("XSMALL"),
									ui:Label(l.PUMP_DOWN),
									ui:HBox(5):PackEnd({
										pumpDownOne,
										pumpDownTen,
										pumpDown100,
									}):SetFont("XSMALL"),
								}),
							})
					})
				)
			})
			:SetColumn(2, {
				cargoListWidget
			})
	)
end

return econTrade

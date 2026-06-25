local M = UnLua.Class()

function M:ReceiveBeginPlay()
    print("=== [UnLua] BP_Test BeginPlay from Lua! ===")
    UE.UKismetSystemLibrary.PrintString(self, "Hello from Lua! BP_Test is alive!!!!!!!!!!!!!!!!!!!!!!!!000", true, true, UE.FLinearColor(0, 1, 0, 1), 10.0)
end

function M:ReceiveTick(DeltaSeconds)
end

return M

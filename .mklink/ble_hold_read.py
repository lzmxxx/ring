import asyncio
from bleak import BleakClient
async def main():
    async with BleakClient("33:22:33:DB:30:A0", timeout=15):
        await asyncio.sleep(20)
asyncio.run(main())

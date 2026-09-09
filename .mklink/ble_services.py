import asyncio
from bleak import BleakClient
async def main():
    async with BleakClient("33:22:33:DB:30:A0", timeout=15) as c:
        for s in c.services:
            print("S", s.uuid)
            for ch in s.characteristics:
                print(" C", ch.uuid, ch.properties)
asyncio.run(main())

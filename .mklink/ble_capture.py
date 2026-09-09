import asyncio
from bleak import BleakClient
ADDR = "33:22:33:DB:30:A0"
CHAR = "0000fec1-0000-1000-8000-00805f9b34fb"
async def main():
    packets = []
    def cb(_, data):
        value = bytes(data).hex(" ").upper()
        packets.append(value)
        print(value, flush=True)
    async with BleakClient(ADDR, timeout=15) as client:
        print("CONNECTED", client.is_connected, flush=True)
        await client.start_notify(CHAR, cb)
        await asyncio.sleep(12)
        await client.stop_notify(CHAR)
    print("COUNT", len(packets), flush=True)
asyncio.run(main())


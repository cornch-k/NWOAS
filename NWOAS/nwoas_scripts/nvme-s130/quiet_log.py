"""Bounded logging policy for the host-mediated NVMe controller.

The serial console is a diagnostic channel.  Emitting several lines for every
storage request throttles the guest, so routine doorbells and successful I/O
commands are sampled while state changes and errors remain lossless.
"""


class QuietLogSampler:
    def __init__(self):
        self.command_count = 0
        self.doorbell_count = 0
        self.other_count = 0

    @staticmethod
    def _sample(count, initial, interval):
        return count <= initial or count % interval == 0

    def should_emit(self, message):
        if message.startswith("CMD "):
            self.command_count += 1
            if " status=0 " not in message:
                return True
            return self._sample(self.command_count, 16, 2048)

        if message.startswith(("MMIO W ", "MMIO R ")):
            fields = message.split()
            try:
                offset = int(fields[2].split("/", 1)[0], 16)
            except (IndexError, ValueError):
                return True
            # Windows toggles INTMS/INTMC (0x0c/0x10) for nearly every
            # completion. They are routine traffic just like queue doorbells.
            # Keep controller configuration registers lossless.
            if offset < 0x1000 and offset not in (0x0C, 0x10):
                return True
            self.doorbell_count += 1
            return self._sample(self.doorbell_count, 16, 4096)

        if message.startswith(("CFS", "EN ", "CREATE ", "BAR ")):
            return True

        self.other_count += 1
        return self._sample(self.other_count, 200, 256)

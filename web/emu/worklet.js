// SPDX-License-Identifier: GPL-3.0-only
// Barge's FM-1 browser simulation. The AudioWorklet drives the device clock and audio workload, following the
// WebAssembly/AudioWorklet approach used by Felucca and X0X, adapted to Barge's small host platform.
const FRAME_MS = 1 / 30;

if (typeof registerProcessor === "function") {
  class BargeProcessor extends AudioWorkletProcessor {
    constructor() {
      super();
      this.ex = null;
      this.lastFrame = 0;
      this.lastDraws = -1;
      this.lastWrites = -1;
      this.inputQueue = [];
      this.nextInputAt = 0;
      this.port.onmessage = (event) => this.onMessage(event.data);
    }

    async onMessage(message) {
      if (message.type === "load") {
        try {
          const { instance } = await WebAssembly.instantiate(message.wasm, {});
          const ex = instance.exports;
          ex._initialize();
          const settings = new Uint8Array(ex.memory.buffer, ex.web_settings_data(), ex.web_settings_size());
          if (Array.isArray(message.settings) && message.settings.length === settings.length)
            settings.set(message.settings);
          ex.web_boot();
          this.ex = ex;
          this.lastWrites = ex.web_settings_writes_count();
          this.port.postMessage({ type: "ready", rate: ex.web_sample_rate() });
        } catch (error) {
          this.port.postMessage({ type: "error", message: String(error?.message || error) });
        }
        return;
      }
      const ex = this.ex;
      if (!ex) return;
      switch (message.type) {
        case "buttons":
        case "keys": this.inputQueue.push(message); break;
        case "encoder": ex.web_encoder(message.role >>> 0, message.delta | 0); break;
        case "master": ex.web_master(message.value >>> 0); break;
        case "midi": ex.web_midi(message.status >>> 0, message.data1 >>> 0, message.data2 >>> 0); break;
      }
    }

    process(_inputs, outputs) {
      const ex = this.ex;
      const output = outputs[0];
      if (!ex) {
        for (const channel of output) channel.fill(0);
        return true;
      }
      const frames = output[0].length;
      let now = ex.web_now_ms();
      while (this.inputQueue.length && now - this.nextInputAt >= 0) {
        const input = this.inputQueue.shift();
        if (input.type === "buttons") ex.web_buttons(input.mask >>> 0);
        else ex.web_keys(input.mask >>> 0);
        this.nextInputAt = now + 24;
        now = ex.web_now_ms();
      }
      ex.web_render(frames);
      const memory = ex.memory.buffer;
      output[0].set(new Float32Array(memory, ex.web_audio_left(), frames));
      if (output[1]) output[1].set(new Float32Array(memory, ex.web_audio_right(), frames));
      if (currentTime - this.lastFrame >= FRAME_MS) {
        this.lastFrame = currentTime;
        const message = { type: "frame" }, transfer = [];
        const draws = ex.web_screen_draws();
        if (draws !== this.lastDraws) {
          this.lastDraws = draws;
          message.screen = new Uint16Array(memory, ex.web_screen(), 240 * 240).slice();
          transfer.push(message.screen.buffer);
        }
        const writes = ex.web_settings_writes_count();
        if (writes !== this.lastWrites) {
          this.lastWrites = writes;
          message.settings = Array.from(new Uint8Array(memory, ex.web_settings_data(), ex.web_settings_size()));
        }
        message.ledButtons = ex.web_led_buttons();
        message.ledKeys = ex.web_led_keys();
        if (message.screen || message.settings || message.ledButtons !== undefined)
          this.port.postMessage(message, transfer);
      }
      return true;
    }
  }
  registerProcessor("barge", BargeProcessor);
}

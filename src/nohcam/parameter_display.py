import numpy as np
import cv2
from PIL import Image, ImageDraw, ImageFont
from OpenGL.GL import *
from typing import Optional, Dict, List


class ParameterDisplayRenderer:
    """Renders Live2D parameter values with bar graphs in the top-right corner."""

    def __init__(self, width: int = 1280, height: int = 720, margin: int = 10):
        self.width = width
        self.height = height
        self.margin = margin
        self.texture_id = None
        self.texture_data = None
        
        # Parameter info: {param_index: {"name": str, "min": float, "max": float}}
        self.param_info: Dict[int, Dict] = {}
        self.param_values: Dict[int, float] = {}
        
        # Display settings
        self.font_size = 12
        self.bar_length = 20
        self.line_height = 18
        self.panel_width = 350
        self.bg_color = (0, 0, 0, 0)  # Transparent
        
        # Cache for brightness detection
        self.last_brightness = 255

    def set_parameters(self, model_obj) -> None:
        """Extract parameter information from Live2D model."""
        param_ids = model_obj.GetParamIds()
        
        # Parameters to display with their typical ranges
        # Note: Live2D library clips values to the actual parameter range internally
        # These ranges are estimated from typical Cubism models
        display_params = {
            "PARAM_ANGLE_X": (-30, 30),
            "PARAM_ANGLE_Y": (-30, 30),    # 実測: -30 to 30 (クリッピング)
            "PARAM_ANGLE_Z": (-30, 30),
            "PARAM_BODY_X": (-15, 15),
            "PARAM_BODY_Y": (-10, 10),
            "PARAM_BODY_Z": (-30, 30),
            "PARAM_ARM_L": (-60, 60),
            "PARAM_ARM_R": (-60, 60),
        }
        
        for i, param_id in enumerate(param_ids):
            param_name_upper = param_id.upper()
            for display_param, (min_val, max_val) in display_params.items():
                if display_param in param_name_upper:
                    self.param_info[i] = {
                        "name": param_id,
                        "min": min_val,
                        "max": max_val,
                    }
                    self.param_values[i] = 0.0
                    break

    def update_parameter_values(self, model_obj) -> None:
        """Update current parameter values from model."""
        for param_idx in self.param_info.keys():
            param_obj = model_obj.GetParameter(param_idx)
            self.param_values[param_idx] = param_obj.value

    def detect_background_brightness(self, cv_image: np.ndarray) -> tuple:
        """Detect background brightness and return text color (R, G, B, A)."""
        if cv_image is None or cv_image.shape[0] == 0 or cv_image.shape[1] == 0:
            return (255, 255, 255, 255)  # Default: white text
        
        # Sample a region from the center-left of the image
        h, w = cv_image.shape[:2]
        sample_h = h // 2
        sample_w = w // 4
        sample_region = cv_image[
            max(0, sample_h - 30):min(h, sample_h + 30),
            max(0, sample_w - 30):min(w, sample_w + 30)
        ]
        
        if sample_region.size == 0:
            return (255, 255, 255, 255)
        
        # Convert BGR to RGB if needed
        if len(cv_image.shape) == 3 and cv_image.shape[2] >= 3:
            sample_rgb = cv2.cvtColor(sample_region, cv2.COLOR_BGR2RGB)
        else:
            sample_rgb = sample_region
        
        # Calculate luminance
        if len(sample_rgb.shape) == 3:
            r, g, b = sample_rgb[:, :, 0], sample_rgb[:, :, 1], sample_rgb[:, :, 2]
            luminance = 0.299 * r.astype(float) + 0.587 * g.astype(float) + 0.114 * b.astype(float)
            avg_luminance = np.mean(luminance)
        else:
            avg_luminance = np.mean(sample_rgb)
        
        self.last_brightness = int(avg_luminance)
        
        # Choose text color based on background brightness
        if avg_luminance > 128:
            return (0, 0, 0, 255)  # Black text on light background
        else:
            return (255, 255, 255, 255)  # White text on dark background

    def render_to_image(self, text_color: tuple) -> Image.Image:
        """Render parameter display to PIL Image."""
        # Calculate panel height based on number of parameters
        num_params = len(self.param_info)
        panel_height = self.line_height * num_params + 10
        
        # Create transparent image
        img = Image.new("RGBA", (self.panel_width, panel_height), (0, 0, 0, 0))
        draw = ImageDraw.Draw(img)
        
        # Try to load a monospace font, fall back to default
        try:
            font = ImageFont.truetype("consolas.ttf", self.font_size)
        except OSError:
            try:
                font = ImageFont.truetype("courier.ttf", self.font_size)
            except OSError:
                font = ImageFont.load_default()
        
        y_offset = 5
        
        # Sort parameters by index for consistent display
        sorted_params = sorted(self.param_info.items())
        
        for param_idx, info in sorted_params:
            param_name = info["name"]
            param_min = info["min"]
            param_max = info["max"]
            param_value = self.param_values.get(param_idx, 0.0)
            
            # Normalize value to 0-1 range
            value_range = param_max - param_min
            if value_range > 0:
                normalized = (param_value - param_min) / value_range
                normalized = max(0, min(1, normalized))  # Clamp to [0, 1]
            else:
                normalized = 0.5
            
            # Build bar graph
            bar_pos = int(normalized * (self.bar_length - 1))
            bar = ["-"] * self.bar_length
            bar[bar_pos] = "*"
            bar_str = "".join(bar)
            
            # Format line
            line = f"{param_name:15} {param_value:7.2f}  {param_min:7.1f}[{bar_str}]{param_max:7.1f}"
            
            # Draw text
            draw.text((5, y_offset), line, fill=text_color, font=font)
            y_offset += self.line_height
        
        return img

    def create_texture(self, width: int, height: int) -> int:
        """Create an OpenGL texture."""
        texture_id = glGenTextures(1)
        glBindTexture(GL_TEXTURE_2D, texture_id)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, None)
        glBindTexture(GL_TEXTURE_2D, 0)
        return texture_id

    def update_texture(self, pil_image: Image.Image) -> None:
        """Update OpenGL texture with PIL image."""
        width, height = pil_image.size
        
        if self.texture_id is None:
            self.texture_id = self.create_texture(width, height)
        
        # Convert PIL image to numpy array (RGBA)
        img_array = np.array(pil_image, dtype=np.uint8)
        
        glBindTexture(GL_TEXTURE_2D, self.texture_id)
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, img_array)
        glBindTexture(GL_TEXTURE_2D, 0)

    def render(self, screen_width: int, screen_height: int) -> None:
        """Render the parameter display as a 2D overlay in top-right corner."""
        if self.texture_id is None or len(self.param_info) == 0:
            return
        
        num_params = len(self.param_info)
        panel_height = self.line_height * num_params + 10
        
        # Save current OpenGL state
        glMatrixMode(GL_PROJECTION)
        glPushMatrix()
        glLoadIdentity()
        glOrtho(0, screen_width, screen_height, 0, -1, 1)
        glMatrixMode(GL_MODELVIEW)
        glPushMatrix()
        glLoadIdentity()
        
        glDisable(GL_DEPTH_TEST)
        glEnable(GL_BLEND)
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
        glEnable(GL_TEXTURE_2D)
        glBindTexture(GL_TEXTURE_2D, self.texture_id)
        
        # Position in top-right corner
        x = screen_width - self.panel_width - self.margin
        y = self.margin
        
        glBegin(GL_QUADS)
        glColor4f(1.0, 1.0, 1.0, 1.0)
        glTexCoord2f(0, 0)
        glVertex2f(x, y)
        glTexCoord2f(1, 0)
        glVertex2f(x + self.panel_width, y)
        glTexCoord2f(1, 1)
        glVertex2f(x + self.panel_width, y + panel_height)
        glTexCoord2f(0, 1)
        glVertex2f(x, y + panel_height)
        glEnd()
        
        glDisable(GL_TEXTURE_2D)
        glDisable(GL_BLEND)
        glEnable(GL_DEPTH_TEST)
        
        # Restore OpenGL state
        glMatrixMode(GL_PROJECTION)
        glPopMatrix()
        glMatrixMode(GL_MODELVIEW)
        glPopMatrix()

    def cleanup(self) -> None:
        """Delete the texture."""
        if self.texture_id is not None:
            glDeleteTextures([self.texture_id])
            self.texture_id = None

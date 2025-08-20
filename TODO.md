- Make the Vulkan abstraction window-agnostic
  (a.k.a. replace the internal GLFW calls with user callbacks that the programmer can supply, even if just by proxying a GLFW call if they so wish)

- Switch from render pass to dynamic rendering, *unless* a really nice, **modular** abstraction can be made

- Figure out why an additional VkImageView is created every time the Swapchain is refreshed
